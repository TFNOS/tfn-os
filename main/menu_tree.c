// The menu: the tree of the groups (the layout of a profile of the manager, the own home of the board).
#include "app_menu_priv.h"

// The menu tree (the profiles from the manager).
// A layout tells which apps the menu shows, in which groups (a group can hold groups), the look of each group, and
// how the favorites work. It is a small text: the manager makes it from a profile and sends it (POST /api/menu,
// web_walkie.c); the board keeps it in /data/menu.txt. No file: the built-in layout (the FAVORITES item, GAMES,
// UTIL, SETTINGS).
//   MENU 1
//   NAME KIDS              the profile name (none: the built-in layout)
//   START TALKIE           the app that opens at the start (none: the menu)
//   FAV GROUP              GROUP: a FAVORITES item on the main menu; TOP: the favorites on the main menu; OFF: none
//   F 0                    the place of the FAVORITES item (on the main menu)
//   G 2 0 4 QUESTS         a group: its id (1 .. 32), its parent (0 = the main menu), its look (0 .. 23), its name
//                          (A-Z, 0-9, a space, - & ! ?; 15 letters)
//   A 2 LAST SHOW          an app: its parent (0 = the main menu), its menu name. The same app can be in more groups
//                          but only once in one group.
//   A 1 *GAMES             THE REST of a built-in group (*GAMES, *UTIL, *SETTINGS): at this place, all
//                          the apps of that group of the built-in menu that no A line names, in the built-in order.
//                          So the apps of a newer firmware come into the menu. One line for each built-in group.
// The lines of one parent are in the order of the menu; a group comes before the lines in it. An app that this
// firmware has not got is left out (a profile can be newer than the firmware). An old firmware (menuv 1) reads the
// new lines too: it skips a *GAMES line (to it, an unknown app: the apps of the rest are missing there) and the second
// place of an app, shows a new look as GAMES, but refuses a text with more than 16 groups (the manager sends such
// texts only to menuv 2 boards).
//   H DIVE                 HOME 2.0 (menuv 3): a hidden app. It shows in no group (also not in a rest line).
//                          An older firmware refuses a text with an H line: the manager sends it only to menuv 3 boards.
// THE OWN HOME OF THE BOARD (HOME 2.0, TFN OS phase 6): the user arranges the menu on the board (menu_home.c). The
// board keeps that layout in /data/home.txt, in this format, with the line "# HOME <hash>" = the hash of the profile
// that it came from. /data/menu.txt stays the profile of the manager (its hash goes to the manager). A new
// profile (another hash) goes into the home with no question (home_merge). A
// forced push (POST /api/menu?force=1) removes home.txt at once: the manager has the last word.

layout_t men_lay;
bool men_lay_hid(const layout_t *L, int i)
{
    return i >= 0 && i < N_CAP && (L->hid[i >> 5] >> (i & 31)) & 1;
}
bool men_home;               // s_lay is the own home of the board (home.txt), not the profile
uint32_t men_home_base;      // the profile hash in home.txt
volatile bool men_home_ask;  // the profile is not the one of the home: ask USE IT / KEEP MINE (menu_home.c)
// The layout as the menu uses it (lay_view): the entries of s_lay, with each rest line as its apps (E_APP entries).
// The export (menu_layout_get) uses s_lay.
lent_t men_view[V_MAX];
int men_nview;
volatile bool men_lay_reload;  // a new layout came (from the web task): the menu takes it at its next update
char men_prof_name[24];        // the profile now (/api/boards), "" = the built-in layout
uint32_t men_prof_hash;        // the FNV-1a hash of the layout file, 0 = the built-in layout
const char *const men_cat_name[N_CATS] = {"GAMES", "UTIL", "SETTINGS"};  // the groups of the built-in layout

// a buffer from PSRAM (internal RAM is short on the AMOLED), else from the normal heap
void *men_ps_alloc(size_t n)
{
    void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
    return p ? p : malloc(n);
}

static uint32_t fnv(const char *s, int n)
{
    uint32_t h = 2166136261u;
    for (int i = 0; i < n; i++) {
        h = (h ^ (uint8_t)s[i]) * 16777619u;
    }
    return h;
}

static void lay_default(layout_t *L)
{
    memset(L, 0, sizeof *L);
    L->fav = FAVM_GROUP;
    L->e[L->ne++] = (lent_t){E_FAV, -1, 0};
    for (int c = 0; c < N_CATS; c++) {
        snprintf(L->g[c].name, sizeof L->g[c].name, "%s", men_cat_name[c]);
        L->g[c].parent = -1;
        L->g[c].style = (uint8_t)c;
        L->e[L->ne++] = (lent_t){E_GRP, -1, (int16_t)c};
    }
    L->ng = N_CATS;
    // (each app by name, not a rest line: the export of the built-in layout stays as before, for the manager)
    for (int i = 0; i < N_ALL; i++) {
        L->e[L->ne++] = (lent_t){E_APP, (int8_t)men_all[i].cat, (int16_t)i};  // (CAT_* = 0 .. 2)
    }
}

static int app_by_name(const char *n)
{
    for (int i = 0; i < N_ALL; i++) {
        if (!strcasecmp(men_all[i].app->name, n)) {
            return i;
        }
    }
    return -1;
}

// true when the layout L has an entry of this kind, parent and ref
static bool lay_has(const layout_t *L, int kind, int parent, int ref)
{
    for (int k = 0; k < L->ne; k++) {
        if (L->e[k].kind == kind && L->e[k].parent == parent && L->e[k].ref == ref) {
            return true;
        }
    }
    return false;
}

// a G line (the id, the id of the parent, the look, the name): a new group; false (and the reason in err) for a bad line
static bool lay_line_group(const char *ln, int line, layout_t *L, int8_t *gid, char *err, int emax)
{
    int id = 0, par = 0, st = 0, k = 0;
    if (sscanf(ln + 2, "%d %d %d %n", &id, &par, &st, &k) < 3 || !k || !ln[2 + k]) {
        snprintf(err, (size_t)emax, "line %d: a bad group", line);
        return false;
    }
    if (id < 1 || id > G_MAX || gid[id] >= 0 || L->ng >= G_MAX || L->ne >= E_MAX) {
        snprintf(err, (size_t)emax, "line %d: a bad group id", line);
        return false;
    }
    if (par < 0 || par > G_MAX || (par && gid[par] < 0)) {
        snprintf(err, (size_t)emax, "line %d: the parent group must come first", line);
        return false;
    }
    int gi = L->ng++;
    lgrp_t *g = &L->g[gi];
    // the name in the menu faces: A-Z, 0-9, a space, - & ! ? ; 15 letters
    int m = 0;
    for (const char *c = ln + 2 + k; *c && m < 15; c++) {
        char ch = (char)toupper((unsigned char)*c);
        if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == ' ' || ch == '-' || ch == '&' || ch == '!' ||
            ch == '?') {
            g->name[m++] = ch;
        }
    }
    g->name[m] = 0;
    if (!m) {
        snprintf(g->name, sizeof g->name, "GROUP");
    }
    g->parent = (int8_t)(par ? gid[par] : -1);
    g->style = (uint8_t)(st >= 0 && st < ST_N ? st : ST_GAMES);
    gid[id] = (int8_t)gi;
    L->e[L->ne++] = (lent_t){E_GRP, g->parent, (int16_t)gi};
    return true;
}

// an A line (the id of the parent, the name of an app or * and a built-in group): a new entry; false (and the reason in
// err) for a bad line
static bool lay_line_app(const char *ln, int line, layout_t *L, const int8_t *gid, int *napps, char *err, int emax)
{
    int par = 0, k = 0;
    if (sscanf(ln + 2, "%d %n", &par, &k) < 1 || !k) {
        snprintf(err, (size_t)emax, "line %d: a bad app line", line);
        return false;
    }
    if (par < 0 || par > G_MAX || (par && gid[par] < 0)) {
        snprintf(err, (size_t)emax, "line %d: the parent group must come first", line);
        return false;
    }
    int8_t pg = (int8_t)(par ? gid[par] : -1);
    const char *nm = ln + 2 + k;
    if (nm[0] == '*') {
        // the rest of a built-in group: once in a layout (a second line, or a name that is not known: skipped)
        for (int c = 0; c < N_CATS; c++) {
            if (strcasecmp(nm + 1, men_cat_name[c])) {
                continue;
            }
            bool dup = false;
            for (int j = 0; j < L->ne; j++) {
                if (L->e[j].kind == E_REST && L->e[j].ref == c) {
                    dup = true;
                }
            }
            if (!dup && L->ne < E_MAX) {
                L->e[L->ne++] = (lent_t){E_REST, pg, (int16_t)c};
            }
        }
        return true;
    }
    int ai = app_by_name(nm);
    // (an app that this firmware has not got; the same app a second time in one group; too many app lines: cut
    // counts them, and a new text with a cut is refused (menu_layout_set); the kept file loads what fits)
    if (ai < 0 || lay_has(L, E_APP, pg, ai)) {
        return true;
    }
    if (*napps >= E_APPS || L->ne >= E_MAX) {
        L->cut++;
        return true;
    }
    (*napps)++;
    L->e[L->ne++] = (lent_t){E_APP, pg, (int16_t)ai};
    return true;
}

// the layout of a text; false (and the reason in err) when the text is not a layout
bool men_lay_parse(const char *t, int len, layout_t *L, char *err, int emax)
{
    memset(L, 0, sizeof *L);
    L->fav = FAVM_GROUP;
    int8_t gid[G_MAX + 1];  // the id in the text -> the group index
    memset(gid, -1, sizeof gid);
    bool head = false, have_fav = false;
    int napps = 0;
    const char *p = t, *end = t + len;
    int line = 0;
    while (p < end) {
        const char *e = memchr(p, '\n', (size_t)(end - p));
        if (!e) {
            e = end;
        }
        char ln[64];
        int n = (int)(e - p);
        if (n > 63) {
            n = 63;
        }
        memcpy(ln, p, (size_t)n);
        ln[n] = 0;
        while (n > 0 && (ln[n - 1] == '\r' || ln[n - 1] == ' ')) {
            ln[--n] = 0;
        }
        p = e + 1;
        line++;
        if (!n || ln[0] == '#') {
            continue;
        }
        if (!head) {
            if (strcmp(ln, "MENU 1")) {
                snprintf(err, (size_t)emax, "not a menu layout (no MENU 1)");
                return false;
            }
            head = true;
            continue;
        }
        if (!strncmp(ln, "NAME", 4) && (ln[4] == ' ' || !ln[4])) {
            int m = 0;
            for (const char *c = ln[4] ? ln + 5 : ""; *c && m < (int)sizeof L->prof - 1; c++) {
                L->prof[m++] = *c;
            }
            L->prof[m] = 0;
            continue;
        }
        if (!strncmp(ln, "START ", 6)) {
            snprintf(L->start, sizeof L->start, "%.15s", ln + 6);
            continue;
        }
        if (!strncmp(ln, "FAV ", 4)) {
            L->fav = !strcmp(ln + 4, "TOP") ? FAVM_TOP : !strcmp(ln + 4, "OFF") ? FAVM_OFF : FAVM_GROUP;
            continue;
        }
        if (ln[0] == 'F' && (ln[1] == ' ' || !ln[1])) {
            if (!have_fav && L->ne < E_MAX) {
                L->e[L->ne++] = (lent_t){E_FAV, -1, 0};
                have_fav = true;
            }
            continue;
        }
        if (ln[0] == 'G' && ln[1] == ' ') {
            if (!lay_line_group(ln, line, L, gid, err, emax)) {
                return false;
            }
            continue;
        }
        if (ln[0] == 'A' && ln[1] == ' ') {
            if (!lay_line_app(ln, line, L, gid, &napps, err, emax)) {
                return false;
            }
            continue;
        }
        if (ln[0] == 'H' && ln[1] == ' ') {
            int ai = app_by_name(ln + 2);  // (an app that this firmware has not got: skipped)
            if (ai >= 0) {
                L->hid[ai >> 5] |= 1u << (ai & 31);
            }
            continue;
        }
        snprintf(err, (size_t)emax, "line %d: not known", line);
        return false;
    }
    if (!head) {
        snprintf(err, (size_t)emax, "empty");
        return false;
    }
    if (L->fav == FAVM_GROUP && !have_fav && L->ne < E_MAX) {
        memmove(&L->e[1], &L->e[0], sizeof(lent_t) * L->ne);
        L->e[0] = (lent_t){E_FAV, -1, 0};
        L->ne++;
    }
    return true;
}

// a layout as a text, as lay_parse reads it; home: the "# HOME <base>" line; returns the length
int men_lay_text(const layout_t *L, char *o, int max, bool home, uint32_t base)
{
    static const char *const FM[3] = {"GROUP", "TOP", "OFF"};
    int n = snprintf(o, (size_t)max, "MENU 1\n");
    if (home && n < max) {
        n += snprintf(o + n, (size_t)(max - n), "# HOME %lu\n", (unsigned long)base);
    }
    if (L->prof[0] && n < max) {
        n += snprintf(o + n, (size_t)(max - n), "NAME %s\n", L->prof);
    }
    if (L->start[0] && n < max) {
        n += snprintf(o + n, (size_t)(max - n), "START %s\n", L->start);
    }
    if (n < max) {
        n += snprintf(o + n, (size_t)(max - n), "FAV %s\n", FM[L->fav < 3 ? L->fav : 0]);
    }
    for (int k = 0; k < L->ne && n < max - 1; k++) {
        const lent_t *e = &L->e[k];
        int par = e->parent + 1;
        if (e->kind == E_FAV) {
            n += snprintf(o + n, (size_t)(max - n), "F 0\n");
        } else if (e->kind == E_GRP) {
            n += snprintf(o + n, (size_t)(max - n), "G %d %d %d %s\n", e->ref + 1, par, L->g[e->ref].style,
                          L->g[e->ref].name);
        } else if (e->kind == E_REST) {
            n += snprintf(o + n, (size_t)(max - n), "A %d *%s\n", par, men_cat_name[e->ref]);
        } else {
            n += snprintf(o + n, (size_t)(max - n), "A %d %s\n", par, men_all_name(e->ref));
        }
    }
    for (int i = 0; i < N_ALL && n < max - 1; i++) {
        if (men_lay_hid(L, i)) {
            n += snprintf(o + n, (size_t)(max - n), "H %s\n", men_all_name(i));
        }
    }
    return n < max ? n : max - 1;
}
// the layout now (the own home of the board when it has one; the built-in layout too)
int menu_layout_get(char *o, int max)
{
    return men_lay_text(&men_lay, o, max, men_home, men_home_base);
}

// the startup app of the profile (main.c, after the start picture); NULL: the menu
const app_t *menu_start_app(void)
{
    if (!men_lay.start[0]) {
        return NULL;
    }
    int i = app_by_name(men_lay.start);
    return i >= 0 ? men_all[i].app : NULL;
}

// a NEW app ("new" in main/apps.json, or new=1 in the manifest of an installed app)
static bool all_new(int i)
{
    return men_builtin_new(men_all[i].app) || men_tfn_new(tfn_entry_of(men_all[i].app));
}
// the view of the layout now (after each new layout): the entries, and in place of each rest line the apps of its
// built-in group that no app entry names, in the ALL[] order (a rest app is never named, so never twice in a group).
// A NEW app of a rest line comes FIRST in its group, before the lines of the layout: with an own order (a profile or
// the home of the board) a new game would be at the end of GAMES. A new app that the layout names stays at its place.
void men_lay_view(void)
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
            named[i] = 1;  // (a hidden app: not in a rest line)
        }
    }
    uint8_t lead[G_MAX + 1];  // the groups (parent + 1) that have their new rest apps already
    memset(lead, 0, sizeof lead);
    men_nview = 0;
    for (int k = 0; k < men_lay.ne && men_nview < V_MAX; k++) {
        const lent_t *e = &men_lay.e[k];
        // (the first line of a group: before it, the new apps of the rest lines of that group)
        int pg = e->parent + 1;
        if (e->parent >= 0 && pg <= G_MAX && !lead[pg]) {
            lead[pg] = 1;
            for (int r = 0; r < men_lay.ne; r++) {
                const lent_t *q = &men_lay.e[r];
                if (q->kind != E_REST || q->parent != e->parent) {
                    continue;
                }
                for (int i = 0; i < N_ALL && men_nview < V_MAX; i++) {
                    if (men_all[i].cat == q->ref && !named[i] && all_new(i)) {
                        men_view[men_nview++] = (lent_t){E_APP, e->parent, (int16_t)i};
                    }
                }
            }
        }
        if (e->kind == E_APP && men_lay_hid(&men_lay, e->ref)) {
            continue;
        }
        if (e->kind != E_REST) {
            men_view[men_nview++] = *e;
            continue;
        }
        for (int i = 0; i < N_ALL && men_nview < V_MAX; i++) {
            if (men_all[i].cat == e->ref && !named[i] && (e->parent < 0 || !all_new(i))) {
                men_view[men_nview++] = (lent_t){E_APP, e->parent, (int16_t)i};
            }
        }
    }
}

// A new profile and an own home: a change of the layout must not make the device ignore profiles, so
// the profile goes INTO the home, with no question. Each group of the profile that the home has not got
// comes in (by its name), and each app line and each rest line of the profile that the home has not got comes at the
// end of its group (the group of the same name in the home). The order of the user stays; the hidden apps of the home
// stay hidden. A forced push of
// the manager still replaces the home.
static void home_merge(layout_t *H, const layout_t *P)
{
    int8_t gmap[G_MAX];
    for (int g = 0; g < P->ng && g < G_MAX; g++) {
        gmap[g] = -1;
        for (int h = 0; h < H->ng; h++) {
            if (!strcmp(H->g[h].name, P->g[g].name)) {
                gmap[g] = (int8_t)h;
                break;
            }
        }
        if (gmap[g] < 0 && H->ng < G_MAX && H->ne < E_MAX) {
            int8_t par = P->g[g].parent >= 0 && P->g[g].parent < g ? gmap[P->g[g].parent] : -1;
            H->g[H->ng] = (lgrp_t){"", par, P->g[g].style};
            snprintf(H->g[H->ng].name, sizeof H->g[H->ng].name, "%s", P->g[g].name);
            H->e[H->ne++] = (lent_t){E_GRP, par, (int16_t)H->ng};
            gmap[g] = (int8_t)H->ng++;
        }
    }
    int added = 0;
    for (int k = 0; k < P->ne && H->ne < E_MAX; k++) {
        const lent_t *e = &P->e[k];
        if (e->kind != E_APP && e->kind != E_REST) {
            continue;
        }
        int8_t par = e->parent >= 0 && e->parent < P->ng ? gmap[e->parent] : -1;
        bool have = e->kind == E_APP && men_lay_hid(H, e->ref);
        for (int j = 0; j < H->ne && !have; j++) {
            have = H->e[j].kind == e->kind && H->e[j].ref == e->ref;
        }
        if (have) {
            continue;
        }
        H->e[H->ne++] = (lent_t){e->kind, par, e->ref};
        added++;
    }
    // The merge does not only add: an app that the profile removed or hid must not stay in the home. The profile
    // also decides what is NOT in the menu; the user keeps the places and the order. An app that the profile hides is hidden in the home; an app that the profile
    // does not show at all (no app line, not in one of its rest lines) and a rest line that the profile has not got
    // leave the home; a group of the home that the profile has not got and that is empty after that leaves too.
    static uint8_t in_p[N_CAP];
    memset(in_p, 0, sizeof in_p);
    for (int k = 0; k < P->ne; k++) {
        const lent_t *e = &P->e[k];
        if (e->kind == E_APP && !men_lay_hid(P, e->ref)) {
            in_p[e->ref] = 1;
        }
        if (e->kind == E_REST) {
            for (int i = 0; i < N_ALL; i++) {
                if (men_all[i].cat == e->ref && !men_lay_hid(P, i)) {
                    in_p[i] = 1;
                }
            }
        }
    }
    int removed = 0, hidden = 0;
    for (int i = 0; i < N_ALL; i++) {
        if (men_lay_hid(P, i) && !men_lay_hid(H, i)) {
            H->hid[i >> 5] |= 1u << (i & 31);
            hidden++;
        }
    }
    int ne = 0;
    for (int k = 0; k < H->ne; k++) {
        const lent_t *e = &H->e[k];
        bool rest_in_p = false;
        for (int j = 0; e->kind == E_REST && j < P->ne && !rest_in_p; j++) {
            rest_in_p = P->e[j].kind == E_REST && P->e[j].ref == e->ref;
        }
        bool drop =
                (e->kind == E_APP && (!in_p[e->ref] || men_lay_hid(H, e->ref))) || (e->kind == E_REST && !rest_in_p);
        if (drop) {
            removed++;
            continue;
        }
        H->e[ne++] = *e;
    }
    H->ne = (uint16_t)ne;
    // the empty groups that the profile has not got (more passes: a group can hold only an empty group)
    for (bool again = true; again;) {
        again = false;
        for (int k = 0; k < H->ne; k++) {
            const lent_t *e = &H->e[k];
            if (e->kind != E_GRP) {
                continue;
            }
            bool in_prof = false, used = false;
            for (int g = 0; g < P->ng && !in_prof; g++) {
                in_prof = !strcmp(P->g[g].name, H->g[e->ref].name);
            }
            for (int j = 0; j < H->ne && !used; j++) {
                used = H->e[j].parent == e->ref;
            }
            if (in_prof || used) {
                continue;
            }
            memmove(&H->e[k], &H->e[k + 1], sizeof(lent_t) * (size_t)(H->ne - k - 1));
            H->ne--;
            removed++;
            again = true;
            break;
        }
    }
    ESP_LOGI("menu", "home: the new profile \"%s\" went into the own home (%d new lines, %d lines out, %d apps hidden)",
             P->prof, added, removed, hidden);
}

// The rest lines of the profile P that the home H has not got come back, at the end of the group with the same name
// (else the group with the name of the built-in group, else the top level). The edit mode cannot remove a rest line,
// but a home can have no rest line (made from the built-in layout, which names each app): then no
// new app comes into its menu. Returns the count of lines that came back.
static int home_rest_fix(layout_t *H, const layout_t *P)
{
    int fixed = 0;
    for (int k = 0; k < P->ne && H->ne < E_MAX; k++) {
        const lent_t *e = &P->e[k];
        if (e->kind != E_REST) {
            continue;
        }
        bool have = false;
        for (int j = 0; j < H->ne && !have; j++) {
            have = H->e[j].kind == E_REST && H->e[j].ref == e->ref;
        }
        if (have) {
            continue;
        }
        const char *pn = e->parent >= 0 && e->parent < P->ng ? P->g[e->parent].name : NULL;
        int par = -1;
        for (int pass = 0; pass < 2 && par < 0; pass++) {
            const char *nm = pass == 0 ? pn : men_cat_name[e->ref];
            if (!nm) {
                continue;
            }
            for (int j = 0; j < H->ne && par < 0; j++) {
                if (H->e[j].kind == E_GRP && !strcmp(H->g[H->e[j].ref].name, nm)) {
                    par = H->e[j].ref;
                }
            }
        }
        H->e[H->ne++] = (lent_t){E_REST, (int8_t)par, e->ref};
        fixed++;
    }
    return fixed;
}

// the own home of the board over the profile in s_lay (lay_load): false when there is none (or it is bad)
static bool home_load(void)
{
    FILE *f = storage_open_read(HOME_FILE);
    if (!f) {
        return false;
    }
    char *buf = men_ps_alloc(MENU_TEXT_MAX + 1);
    layout_t *H = men_ps_alloc(sizeof *H);
    int n = buf && H ? (int)fread(buf, 1, MENU_TEXT_MAX, f) : 0;
    fclose(f);
    char err[64];
    bool ok = false;
    if (n > 0 && men_lay_parse(buf, n, H, err, sizeof err)) {
        buf[n] = 0;
        const char *b = strstr(buf, "# HOME ");
        men_home_base = b ? (uint32_t)strtoul(b + 7, NULL, 10) : 0;
        // the profile keeps the start app and the way of the favorites (the manager decides them)
        snprintf(H->start, sizeof H->start, "%s", men_lay.start);
        snprintf(H->prof, sizeof H->prof, "%s", men_lay.prof);
        H->fav = men_lay.fav;
        if (H->fav == FAVM_GROUP && !lay_has(H, E_FAV, -1, 0) && H->ne < E_MAX) {
            memmove(&H->e[1], &H->e[0], sizeof(lent_t) * H->ne);
            H->e[0] = (lent_t){E_FAV, -1, 0};
            H->ne++;
        }
        // (the built-in layout names each app and has no rest line; for the home it has a rest line in each built-in
        // group, so a home made from it gets the apps that come later. Its export stays as before.)
        const layout_t *P = &men_lay;
        layout_t *Q = men_prof_hash ? NULL : men_ps_alloc(sizeof *Q);
        if (Q) {
            *Q = men_lay;
            for (int c = 0; c < N_CATS && c < Q->ng && Q->ne < E_MAX; c++) {
                Q->e[Q->ne++] = (lent_t){E_REST, (int8_t)c, (int16_t)c};
            }
            P = Q;
        }
        bool merge = men_home_base != men_prof_hash;
        if (merge) {
            home_merge(H, P);
        }
        int fixed = home_rest_fix(H, P);
        if (fixed) {
            ESP_LOGW("menu", "home: %d rest lines of the profile came back", fixed);
        }
        free(Q);
        men_lay = *H;
        men_home = ok = true;
        if (merge || fixed) {
            men_home_save();  // (the home now has the hash of this profile)
        }
    } else if (n > 0) {
        ESP_LOGW("menu", "the home file is bad (%s): the profile", err);
    }
    free(buf);
    free(H);
    return ok;
}

// the layout from the files (the app task: at the start, and after a new layout came): the profile, then the own home
void men_lay_load(void)
{
    men_prof_hash = 0;
    men_prof_name[0] = 0;
    men_home = false;
    men_home_ask = false;
    bool have = false;
    FILE *f = storage_open_read(MENU_FILE);
    if (f) {
        char *buf = men_ps_alloc(MENU_TEXT_MAX);
        int n = buf ? (int)fread(buf, 1, MENU_TEXT_MAX, f) : 0;
        fclose(f);
        char err[64];
        if (n > 0 && men_lay_parse(buf, n, &men_lay, err, sizeof err)) {
            men_prof_hash = fnv(buf, n);
            snprintf(men_prof_name, sizeof men_prof_name, "%s", men_lay.prof);
            have = true;
        } else if (n > 0) {
            ESP_LOGW("menu", "the layout file is bad (%s): the built-in layout", err);
        }
        free(buf);
    }
    if (!have) {
        lay_default(&men_lay);
    }
    home_load();
    men_lay_view();
}

// the manager removes the own home of the board (a forced push, or the console "menu drop"): the profile shows
void menu_home_drop(void);
void menu_home_drop(void)
{
    remove(HOME_FILE);
    men_lay_reload = true;
}
bool menu_home_own(void);
bool menu_home_own(void)
{
    return men_home;
}

// a new layout (the web task): checked, kept in the file; the menu takes it at its next update. "RESET": back to the
// built-in layout. false: not a layout (the reason in err), and nothing changes.
bool menu_layout_set(const char *t, int len, char *err, int emax)
{
    if (len >= 5 && !strncmp(t, "RESET", 5)) {
        remove(MENU_FILE);
        men_prof_hash = 0;
        men_prof_name[0] = 0;
        men_lay_reload = true;
        return true;
    }
    if (len > MENU_TEXT_MAX) {
        snprintf(err, (size_t)emax, "too long");
        return false;
    }
    layout_t *L = men_ps_alloc(sizeof *L);
    if (!L) {
        snprintf(err, (size_t)emax, "no memory");
        return false;
    }
    bool ok = men_lay_parse(t, len, L, err, emax);
    if (ok && L->cut) {
        snprintf(err, (size_t)emax, "too many app lines (%d over %d)", L->cut, E_APPS);
        ok = false;
    }
    if (ok) {
        if (!storage_write(MENU_FILE, t, (size_t)len)) {
            snprintf(err, (size_t)emax, "cannot write the file");
            ok = false;
        }  // (C-08)
        if (ok) {
            men_prof_hash = fnv(t, len);
            snprintf(men_prof_name, sizeof men_prof_name, "%s", L->prof);
            men_lay_reload = true;
        }
    }
    free(L);
    return ok;
}
const char *menu_profile(void)
{
    return men_prof_name;
}
uint32_t menu_profile_hash(void)
{
    return men_prof_hash;
}

// ---------------------------------------------------------------- the groups now
bool men_fav_on(void)
{
    return men_lay.fav != FAVM_OFF;
}
int men_gparent(int g)
{
    return g >= 0 && g < men_lay.ng ? men_lay.g[g].parent : -1;
}
int men_gstyle(int g)
{
    return g == LV_FAV ? ST_STAR : g >= 0 && g < men_lay.ng ? men_lay.g[g].style : ST_GAMES;
}
const char *men_gname(int g)
{
    return g == LV_FAV ? "FAVORITES" : g >= 0 && g < men_lay.ng ? men_lay.g[g].name : "TFN OS";
}
// the look of the level above the group g (the BACK tile of g): its parent group, or the main menu
int men_gstyle_up(int g)
{
    int p = men_gparent(g);
    return p >= 0 ? men_gstyle(p) : ST_TOP;
}
// true when the group g is p or holds p (at any depth)
bool men_g_under(int p, int g)
{
    for (int d = 0; p >= 0 && d <= G_MAX; p = men_lay.g[p].parent, d++) {
        if (p == g) {
            return true;
        }
    }
    return false;
}
// the apps in each group (and in the groups in it) that this board can run; [LV_FAV]: the favorites in the layout.
// An app in two places of one group (a copy in two of its groups) counts once.
void men_count_groups(void)
{
    static uint32_t seen[G_MAX + 1][(N_CAP + 31) / 32];
    memset(men_cat_count, 0, sizeof men_cat_count);
    memset(seen, 0, sizeof seen);
    for (int k = 0; k < men_nview; k++) {
        const lent_t *e = &men_view[k];
        if (e->kind != E_APP || !men_can_run(e->ref)) {
            continue;
        }
        uint32_t bit = 1u << (e->ref & 31);
        int w = e->ref >> 5;
        for (int g = e->parent, d = 0; g >= 0 && d <= G_MAX; g = men_lay.g[g].parent, d++) {
            if (!(seen[g][w] & bit)) {
                seen[g][w] |= bit;
                men_cat_count[g]++;
            }
        }
        if (men_fav_on() && men_fav_index(men_all[e->ref].app) >= 0 && !(seen[LV_FAV][w] & bit)) {
            seen[LV_FAV][w] |= bit;
            men_cat_count[LV_FAV]++;
        }
    }
}
// SETUP is in the menu (else a hold of 8 s on an empty place opens it: menu_spot.c)
bool men_setup_in_menu(void)
{
    for (int k = 0; k < men_nview; k++) {
        if (men_view[k].kind == E_APP && men_all[men_view[k].ref].app == &app_settings &&
            (men_view[k].parent < 0 || men_cat_count[men_view[k].parent] > 0)) {
            return true;
        }
    }
    return false;
}
