// The menu: HOME 2.0, the changes of the layout (the groups, the moves, the hidden apps, the save, the undo).
#include "menu_home_priv.h"

// ---------------------------------------------------------------- the layout operations (on s_lay)
int men_ai_of(const app_t *a)
{
    for (int i = 0; i < N_ALL; i++) {
        if (men_all[i].app == a) {
            return i;
        }
    }
    return -1;
}
int men_ent_find(int kind, int parent, int ref)
{
    for (int k = 0; k < men_lay.ne; k++) {
        if (men_lay.e[k].kind == kind && men_lay.e[k].parent == parent && men_lay.e[k].ref == ref) {
            return k;
        }
    }
    return -1;
}
int men_ent_any(int kind, int ref)
{
    for (int k = 0; k < men_lay.ne; k++) {
        if (men_lay.e[k].kind == kind && men_lay.e[k].ref == ref) {
            return k;
        }
    }
    return -1;
}
static void ent_del(int k)
{
    memmove(&men_lay.e[k], &men_lay.e[k + 1], sizeof(lent_t) * (size_t)(men_lay.ne - k - 1));
    men_lay.ne--;
}
static bool ent_ins(int at, lent_t v)
{
    if (men_lay.ne >= E_MAX) {
        return false;
    }
    if (at > men_lay.ne) {
        at = men_lay.ne;
    }
    memmove(&men_lay.e[at + 1], &men_lay.e[at], sizeof(lent_t) * (size_t)(men_lay.ne - at));
    men_lay.e[at] = v;
    men_lay.ne++;
    return true;
}
// each rest line: its apps now as app lines before it (the line stays: the apps of a newer firmware come there). The
// menu shows the same; now each app has an entry that the operations can move.
void men_hm_materialize(void)
{
    uint8_t named[N_CAP];
    memset(named, 0, sizeof named);
    for (int k = 0; k < men_lay.ne; k++) {
        if (men_lay.e[k].kind == E_APP) {
            named[men_lay.e[k].ref] = 1;
        }
    }
    for (int i = 0; i < N_ALL; i++) {
        if (men_lay_hid(&men_lay, i)) {
            named[i] = 1;
        }
    }
    for (int k = 0; k < men_lay.ne; k++) {
        if (men_lay.e[k].kind != E_REST) {
            continue;
        }
        lent_t r = men_lay.e[k];
        for (int i = 0; i < N_ALL; i++) {
            if (men_all[i].cat == r.ref && !named[i] && ent_ins(k, (lent_t){E_APP, r.parent, (int16_t)i})) {
                named[i] = 1;
                k++;
            }
        }
    }
}
// the order of the lines: each group before its lines (the order of each level stays). Lines of a group that is gone
// go too.
void men_hm_norm(void)
{
    lent_t *o = men_ps_alloc(sizeof(lent_t) * E_MAX);
    if (!o) {
        return;
    }
    int n = 0, d = 0, sp[G_MAX + 2], sk[G_MAX + 2];
    uint8_t seen[G_MAX];
    memset(seen, 0, sizeof seen);
    sp[0] = -1;
    sk[0] = 0;
    while (d >= 0) {
        int k = sk[d];
        while (k < men_lay.ne && men_lay.e[k].parent != sp[d]) {
            k++;
        }
        if (k >= men_lay.ne) {
            d--;
            continue;
        }
        sk[d] = k + 1;
        const lent_t *e = &men_lay.e[k];
        if (e->kind == E_GRP && (e->ref < 0 || e->ref >= G_MAX || seen[e->ref])) {
            continue;
        }
        o[n++] = *e;
        if (e->kind == E_GRP && d < G_MAX) {
            seen[e->ref] = 1;
            men_lay.g[e->ref].parent = (int8_t)sp[d];
            d++;
            sp[d] = e->ref;
            sk[d] = 0;
        }
    }
    memcpy(men_lay.e, o, sizeof(lent_t) * (size_t)n);
    men_lay.ne = (uint16_t)n;
    free(o);
}
// a group slot for a new group: one with no line, else a new one; -1: all 32 are used
static int hm_new_slot(void)
{
    for (int g = 0; g < men_lay.ng; g++) {
        if (men_ent_any(E_GRP, g) < 0) {
            return g;
        }
    }
    return men_lay.ng < G_MAX ? men_lay.ng++ : -1;
}
bool men_hm_has_slot(void)
{
    if (men_lay.ng < G_MAX) {
        return true;
    }
    for (int g = 0; g < men_lay.ng; g++) {
        if (men_ent_any(E_GRP, g) < 0) {
            return true;
        }
    }
    return false;
}
static bool hm_name_used(const char *n)
{
    for (int g = 0; g < men_lay.ng; g++) {
        if (men_ent_any(E_GRP, g) >= 0 && !strcmp(men_lay.g[g].name, n)) {
            return true;
        }
    }
    return false;
}
// a new group at the place `at` of s_lay.e, in the level p: its slot, or -1
int men_hm_group_make(int at, int p, const char *base)
{
    int g = hm_new_slot();
    if (g < 0) {
        return -1;
    }
    char n[16];
    snprintf(n, sizeof n, "%s", base);
    for (int k = 2; hm_name_used(n) && k < 99; k++) {
        snprintf(n, sizeof n, "%.12s %d", base, k);
    }
    snprintf(men_lay.g[g].name, sizeof men_lay.g[g].name, "%s", n);
    men_lay.g[g].parent = (int8_t)p;
    // the look: the first of these that no group has now
    static const uint8_t LOOKS[] = {ST_STAR, ST_MUSIC, ST_PUZZLE, ST_RETRO, ST_SPACE,
                                    ST_SEA,  ST_CALM,  ST_ACTION, ST_HEART, ST_NEW};
    uint8_t st = LOOKS[0];
    for (int i = 0; i < (int)sizeof LOOKS; i++) {
        bool used = false;
        for (int h = 0; h < men_lay.ng; h++) {
            if (h != g && men_ent_any(E_GRP, h) >= 0 && men_lay.g[h].style == LOOKS[i]) {
                used = true;
            }
        }
        if (!used) {
            st = LOOKS[i];
            break;
        }
    }
    men_lay.g[g].style = st;
    if (!ent_ins(at, (lent_t){E_GRP, (int8_t)p, (int16_t)g})) {
        return -1;
    }
    return g;
}
// move the line k before the line `before` (-1: the end of its level)
void men_op_move(int k, int before)
{
    lent_t v = men_lay.e[k];
    ent_del(k);
    if (before > k) {
        before--;
    }
    ent_ins(before < 0 ? men_lay.ne : before, v);
    men_hm_norm();
}
// the line k into the group g (the end of the group); false: not possible (a group into itself)
bool men_op_to_group(int k, int g)
{
    lent_t v = men_lay.e[k];
    if (v.kind == E_GRP && men_g_under(g, v.ref)) {
        return false;
    }
    if (v.kind == E_FAV) {
        return false;
    }
    ent_del(k);
    if (v.kind == E_APP && men_ent_find(E_APP, g, v.ref) >= 0) {
        men_hm_norm();
        return true;
    }  // (it is there already)
    v.parent = (int8_t)g;
    if (v.kind == E_GRP) {
        men_lay.g[v.ref].parent = (int8_t)g;
    }
    ent_ins(men_lay.ne, v);
    men_hm_norm();
    return true;
}
// the line k out of its group: into the level above, after the group
void men_op_out(int k)
{
    lent_t v = men_lay.e[k];
    int p = v.parent;
    if (p < 0) {
        return;
    }
    int pp = men_gparent(p);
    ent_del(k);
    if (v.kind == E_APP && men_ent_find(E_APP, pp, v.ref) >= 0) {
        men_hm_norm();
        return;
    }
    v.parent = (int8_t)pp;
    if (v.kind == E_GRP) {
        men_lay.g[v.ref].parent = (int8_t)pp;
    }
    int gk = men_ent_find(E_GRP, pp, p);
    ent_ins(gk >= 0 ? gk + 1 : men_lay.ne, v);
    men_hm_norm();
}
// a new group of the apps of the lines kd (dragged) and kt (the target), at the place of the target; the group or -1
int men_op_new_group(int kd, int kt)
{
    lent_t d = men_lay.e[kd], t = men_lay.e[kt];
    int p = t.parent;
    int cd = men_all[d.ref].cat, ct = men_all[t.ref].cat;
    const char *base = cd == ct && cd == CAT_GAMES ? "MY GAMES" : cd == ct && cd == CAT_UTIL ? "MY TOOLS" : "MY APPS";
    int g = men_hm_group_make(kt, p, base);
    if (g < 0) {
        return -1;
    }
    // (the group line is now at kt: the target is at kt + 1, the dragged one after kt when it was after)
    int k1 = men_ent_find(E_APP, p, t.ref), k2 = men_ent_find(E_APP, d.parent, d.ref);
    if (k1 >= 0) {
        men_lay.e[k1].parent = (int8_t)g;
    }
    if (k2 >= 0) {
        lent_t v = men_lay.e[k2];
        ent_del(k2);
        v.parent = (int8_t)g;
        if (men_ent_find(E_APP, g, v.ref) < 0) {
            ent_ins(men_lay.ne, v);
        }
    }
    men_hm_norm();
    return g;
}
int men_hid_count(void)
{
    int n = 0;
    for (int i = 0; i < N_ALL; i++) {
        if (men_lay_hid(&men_lay, i) && men_can_run(i)) {
            n++;
        }
    }
    return n;
}
void men_op_hide(int ai)
{
    for (int k = men_lay.ne - 1; k >= 0; k--) {
        if (men_lay.e[k].kind == E_APP && men_lay.e[k].ref == ai) {
            ent_del(k);
        }
    }
    men_lay.hid[ai >> 5] |= 1u << (ai & 31);
}
// the hidden apps come back: before the rest line of their built-in group, else at the end of the group with the name
// of their built-in group (GAMES, UTIL, SETTINGS), else at the end of the top level
void men_op_show_all(void)
{
    for (int i = 0; i < N_ALL; i++) {
        if (!men_lay_hid(&men_lay, i)) {
            continue;
        }
        men_lay.hid[i >> 5] &= ~(1u << (i & 31));
        int r = -1, p = -1;
        for (int k = 0; k < men_lay.ne && r < 0; k++) {
            if (men_lay.e[k].kind == E_REST && men_lay.e[k].ref == men_all[i].cat) {
                r = k;
            }
        }
        if (r >= 0) {
            p = men_lay.e[r].parent;
        } else {
            for (int g = 0; g < men_lay.ng && p < 0; g++) {
                if (men_ent_any(E_GRP, g) >= 0 && !strcmp(men_lay.g[g].name, men_cat_name[men_all[i].cat])) {
                    p = g;
                }
            }
        }
        if (men_ent_find(E_APP, p, i) < 0) {
            ent_ins(r >= 0 ? r : men_lay.ne, (lent_t){E_APP, (int8_t)p, (int16_t)i});
        }
    }
    men_hm_norm();
}
// a group goes; its lines come into the level above, at its place
void men_op_apart(int g)
{
    int gk = men_ent_any(E_GRP, g);
    if (gk < 0) {
        return;
    }
    int pp = men_lay.e[gk].parent, n = 0;
    lent_t *kids = men_ps_alloc(sizeof(lent_t) * E_MAX);
    if (!kids) {
        return;
    }
    for (int k = 0; k < men_lay.ne; k++) {
        if (men_lay.e[k].parent == g) {
            kids[n++] = men_lay.e[k];
        }
    }
    for (int k = men_lay.ne - 1; k >= 0; k--) {
        if (men_lay.e[k].parent == g) {
            ent_del(k);
        }
    }
    gk = men_ent_any(E_GRP, g);
    int at = gk + 1;
    for (int j = 0; j < n; j++) {
        lent_t v = kids[j];
        v.parent = (int8_t)pp;
        if (v.kind == E_GRP) {
            men_lay.g[v.ref].parent = (int8_t)pp;
        }
        if (v.kind == E_APP && men_ent_find(E_APP, pp, v.ref) >= 0) {
            continue;  // (it is in the level above already)
        }
        if (ent_ins(at, v)) {
            at++;
        }
    }
    free(kids);
    gk = men_ent_any(E_GRP, g);
    if (gk >= 0) {
        ent_del(gk);
    }
    men_hm_norm();
}
// the groups with no line in them go (DONE), also groups in groups
void men_hm_drop_empty(void)
{
    for (bool again = true; again;) {
        again = false;
        for (int k = 0; k < men_lay.ne; k++) {
            if (men_lay.e[k].kind != E_GRP) {
                continue;
            }
            int g = men_lay.e[k].ref;
            bool any = false;
            for (int j = 0; j < men_lay.ne && !any; j++) {
                any = men_lay.e[j].parent == g;
            }
            if (!any) {
                ent_del(k);
                again = true;
                break;
            }
        }
    }
}

// the own home: s_lay into home.txt, with the hash of the profile now
bool men_home_save(void)
{
    char *b = men_ps_alloc(MENU_TEXT_MAX);
    if (!b) {
        return false;
    }
    int n = men_lay_text(&men_lay, b, MENU_TEXT_MAX, true, men_prof_hash);
    bool ok = storage_write(HOME_FILE, b, (size_t)n);  // (C-08: no cut home.txt after a power cut)
    free(b);
    if (ok) {
        men_home = true;
        men_home_base = men_prof_hash;
    }
    ESP_LOGI("menu", "home: %s (%d bytes)", ok ? "saved" : "NOT SAVED", n);
    return ok;
}
// the menu after a change of s_lay out of the edit mode (the sheet): the view, the counts, the items
void men_hm_after(void)
{
    men_lay_view();
    men_build_items();
    men_spot_clamp();
}
// a group by its name (the indexes change when a text is read again); -1: none
int men_hm_group_by_name(const char *n)
{
    for (int g = 0; g < men_lay.ng; g++) {
        if (!strcmp(men_lay.g[g].name, n) && men_ent_any(E_GRP, g) >= 0) {
            return g;
        }
    }
    return -1;
}
// the layout again from a text (UNDO, a new app list): the edit level stays when its group is there
void men_hm_reparse(const char *t)
{
    char lev[16] = "";
    if (men_hm_lev >= 0 && men_hm_lev < men_lay.ng) {
        snprintf(lev, sizeof lev, "%s", men_lay.g[men_hm_lev].name);
    }
    layout_t *L = men_ps_alloc(sizeof *L);
    char err[64];
    if (L && men_lay_parse(t, (int)strlen(t), L, err, sizeof err)) {
        men_lay = *L;
    }
    free(L);
    men_hm_lev = lev[0] ? men_hm_group_by_name(lev) : -1;
    men_lay_view();
}
char *men_hm_text_now(void)
{
    char *b = men_ps_alloc(MENU_TEXT_MAX + 1);
    if (b) {
        b[men_lay_text(&men_lay, b, MENU_TEXT_MAX, false, 0)] = 0;
    }
    return b;
}
void men_undo_push(void)
{
    char *b = men_hm_text_now();
    if (!b) {
        return;
    }
    if (men_nundo == UNDO_MAX) {
        free(men_undo[0]);
        memmove(&men_undo[0], &men_undo[1], sizeof(char *) * (UNDO_MAX - 1));
        men_nundo--;
    }
    men_undo[men_nundo++] = b;
}
void men_undo_clear(void)
{
    while (men_nundo > 0) {
        free(men_undo[--men_nundo]);
    }
}
