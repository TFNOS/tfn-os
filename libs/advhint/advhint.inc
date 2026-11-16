// The hints of BEGINNER mode of the adventures: AFTER HOURS, AFTER HOURS 3D, NIGHT TAPES, STUCK AT 99% and
// THE TURNING WOOD. One copy of the code; each game includes it after its rules, its logic and its hints file
// (<game>_hints.inc: HINTS[] = the hand-written "how" and "answer" of each action, HINT_MODE[] = how to set each mode),
// with these macros:
//   HN_ACT(a)       a pointer to the action a (the fields place, kind, need, gives, text)
//   HN_AMASK(A)     the modes of an action (its mode mask)
//   HN_NACT, HN_NITEM, HN_NFACT, HN_NPLACE, HN_FACTBIT(f)
//   HN_PLACE(p)     the name of a place in the language now; HN_ITEM(i): the name of an object
//   HN_MODE_OK(m)   the mode now is in the mask m
//   HN_HAS_WAY      1: the game has way_to() and path_state(to) (the search of the way); 0: not
//   HN_PATH(i), HN_NPATH, HN_PMASK(P)   the paths and the mode mask of a path (only with HN_HAS_WAY)
//   HN_CLUE_BIT(b)  the bit b is a clue fact (default: the action that gives it is of the kind K_SEE)
// The game gives: T (pos, bits, seen), s_lang, TR(), next_goal(), reachable_now(), text_wrap() with s_wrap_dry, bit64()
// (a test of one bit of a 64-bit mask with 32-bit shifts: a 64-bit shift by a variable needs __lshrdi3, not in the ABI).
//
// The pages of a hint for the next goal. The hint shows what to do, not the RESULT of the next action (for example
// "The cold water flows to the servers."):
//   WHERE   only when the goal is not here: "Go to <place>." (the map jumps there with a double tap), or "Find
//           <place>." with the seen place where its way starts; a way that a mode closes gives the mode line
//   WHAT    the hand-written "how" + the lines of the rules: the mode, the objects that are not in the bag, a step that
//           must come first
//   CLUE    a puzzle: the text of each of its clues and the place of it (a clue not found yet: where it is)
//   ANSWER  the full answer of a puzzle (hand-written)
#ifndef HN_CLUE_BIT
#define HN_CLUE_BIT(b) (hn_giver(b) >= 0 && HN_ACT(hn_giver(b))->kind == K_SEE)
#endif

static void hn_cat(char *h, int len, const char *s)
{
    if (!s || !s[0]) {
        return;
    }
    const int n = (int)strlen(h);
    if (n + 2 < len) {
        snprintf(h + n, (size_t)(len - n), "%s%s", n ? " " : "", s);
    }
}
static int hn_first_mode(int mask)
{
    for (int m = 0; m < 4; m++) {
        if (mask >> m & 1) {
            return m;
        }
    }
    return 0;
}
// the action that gives a bit (an object or a fact); -1: none
static int hn_giver(int bit)
{
    for (int a = 0; a < HN_NACT; a++) {
        if (bit64(HN_ACT(a)->gives, bit)) {
            return a;
        }
    }
    return -1;
}
// the line of a mode ("Set the building to SERVICE on ..."); NULL: the game has no such line
static const char *hn_mode_line(int mask)
{
    return HINT_MODE[hn_first_mode(mask)][s_lang];
}

#if HN_HAS_WAY
// the mode that closes the way from here to gp (the first closed path on the way); -1: no mode closes it
static int hn_way_mode(int gp)
{
    uint8_t way[HN_NPLACE];
    const int k = way_to(gp, way, HN_NPLACE, false);
    int from = T.pos;
    for (int i = 0; i < k; i++) {
        const int to = way[i];
        for (int j = 0; j < HN_NPATH; j++) {
            const int a = HN_PATH(j)->a, b = HN_PATH(j)->b;
            if (!((a == from && b == to) || (a == to && b == from))) {
                continue;
            }
            if (!HN_MODE_OK(HN_PMASK(HN_PATH(j)))) {
                return hn_first_mode(HN_PMASK(HN_PATH(j)));
            }
        }
        from = to;
    }
    return -1;
}
#endif

// WHERE: the place of the goal (not here). A place on the map: "Go to <place>." A place that the player never saw (the
// map does not show it): the seen place where its way starts.
static void hint_where(int gp, char *h, int len)
{
    const char *goal = HN_PLACE(gp);
    h[0] = 0;
    if (T.seen >> gp & 1) {
        if (reachable_now() >> gp & 1) {
            snprintf(h, (size_t)len, TR("Go to %s.", "לכו אל %s."), goal);
            return;
        }
        snprintf(h, (size_t)len, TR("Go to %s. The way there is closed now.", "לכו אל %s. הדרך לשם סגורה עכשיו."),
                 goal);
#if HN_HAS_WAY
        const int m = hn_way_mode(gp);
        if (m >= 0) {
            hn_cat(h, len, HINT_MODE[m][s_lang]);
        }
#endif
        return;
    }
#if HN_HAS_WAY
    uint8_t way[HN_NPLACE];
    int k = way_to(gp, way, HN_NPLACE, true);
    if (!k) {
        k = way_to(gp, way, HN_NPLACE, false);
    }
    if (!k) {
        snprintf(h, (size_t)len, TR("Find %s.", "מצאו את %s."), goal);
        return;
    }
    int door = T.pos, nx = gp;
    for (int i = 0; i < k; i++) {
        if (!(T.seen >> way[i] & 1)) {
            nx = way[i];
            break;
        }
        door = way[i];
    }
    const char *find = HN_PLACE(nx), *at = HN_PLACE(door);
    const uint8_t keep = T.pos;
    T.pos = (uint8_t)door;
    const bool open = path_state(nx) == 1;
    T.pos = keep;
    const bool here = door == T.pos;
    int n;
    if (nx == gp && here) {
        n = snprintf(h, (size_t)len, TR("Find %s. Its way starts here, in %s", "מצאו את %s. הדרך אליו מתחילה כאן, ב%s"),
                     goal, at);
    } else if (nx == gp) {
        n = snprintf(h, (size_t)len, TR("Find %s. Its way starts in %s", "מצאו את %s. הדרך אליו מתחילה ב%s"), goal, at);
    } else if (here) {
        n = snprintf(h, (size_t)len,
                     TR("Find %s. Start with the way to %s, here in %s", "מצאו את %s. התחילו בדרך אל %s, כאן ב%s"),
                     goal, find, at);
    } else {
        n = snprintf(h, (size_t)len,
                     TR("Find %s. Start with the way to %s, in %s", "מצאו את %s. התחילו בדרך אל %s, ב%s"), goal, find,
                     at);
    }
    if (n > 0 && n < len) {
        snprintf(h + n, (size_t)(len - n), "%s", open ? "." : TR(", but it is closed now.", ", אבל היא סגורה עכשיו."));
    }
    if (!open) {
        const int m = hn_way_mode(gp);
        if (m >= 0) {
            hn_cat(h, len, HINT_MODE[m][s_lang]);
        }
    }
#else
    snprintf(h, (size_t)len, TR("Find %s.", "מצאו את %s."), goal);
#endif
}

// WHAT: what to do at the place of the goal g
static void hint_what(int g, char *h, int len)
{
    h[0] = 0;
    hn_cat(h, len, HINTS[g].how[s_lang] ? HINTS[g].how[s_lang] : HN_ACT(g)->text[s_lang]);
    if (!HN_MODE_OK(HN_AMASK(HN_ACT(g)))) {
        hn_cat(h, len, hn_mode_line(HN_AMASK(HN_ACT(g))));
    }
    char l[400], miss[200] = "";
    for (int i = 0; i < HN_NITEM; i++) {
        if (!bit64(HN_ACT(g)->need, i) || bit64(T.bits, i)) {
            continue;
        }
        const int n = (int)strlen(miss);
        snprintf(miss + n, sizeof miss - (size_t)n, "%s%s", n ? ", " : "", HN_ITEM(i));
    }
    if (miss[0]) {
        snprintf(l, sizeof l, TR("You still need: %s.", "עוד חסר לכם: %s."), miss);
        hn_cat(h, len, l);
    }
    for (int f = 0; f < HN_NFACT; f++) {
        const int b = HN_FACTBIT(f);
        if (!bit64(HN_ACT(g)->need, b)) {
            continue;
        }
        const int gv = hn_giver(b);
        if (gv < 0 || gv == g) {
            continue;
        }
        if (HN_CLUE_BIT(b) || bit64(T.bits, b)) {
            continue;
        }
        const char *pl = HN_ACT(gv)->place < HN_NPLACE ? HN_PLACE(HN_ACT(gv)->place) : TR("the bag", "התיק");
        snprintf(l, sizeof l, TR("First, in %s: %s", "קודם, ב%s: %s"), pl,
                 HINTS[gv].how[s_lang] ? HINTS[gv].how[s_lang] : "");
        hn_cat(h, len, l);
    }
}

// the clue facts of the action g (a puzzle): a mask of the fact numbers
static uint32_t hn_clues(int g)
{
    uint32_t m = 0;
    if (HN_ACT(g)->kind != K_PUZZLE) {
        return 0;
    }
    for (int f = 0; f < HN_NFACT && f < 32; f++) {
        const int b = HN_FACTBIT(f);
        if (bit64(HN_ACT(g)->need, b) && HN_CLUE_BIT(b) && hn_giver(b) >= 0 && hn_giver(b) != g) {
            m |= 1u << f;
        }
    }
    return m;
}
// CLUE: each clue of the puzzle g; a clue that the player has: its text and its place; else: where to find it
static void hint_clue(int g, char *h, int len)
{
    const uint32_t m = hn_clues(g);
    char l[400];
    h[0] = 0;
    for (int f = 0; f < HN_NFACT && f < 32; f++) {
        if (!(m >> f & 1)) {
            continue;
        }
        const int b = HN_FACTBIT(f), gv = hn_giver(b);
        const char *pl = HN_ACT(gv)->place < HN_NPLACE ? HN_PLACE(HN_ACT(gv)->place) : TR("the bag", "התיק");
        if (bit64(T.bits, b)) {
            snprintf(l, sizeof l, TR("The clue from %s: %s", "הרמז מ%s: %s"), pl, HN_ACT(gv)->text[s_lang]);
        } else {
            snprintf(l, sizeof l, TR("A clue that you did not find yet is in %s.", "רמז שעוד לא מצאתם נמצא ב%s."), pl);
        }
        hn_cat(h, len, l);
    }
}

// the pages of the hint of the goal g: 0 WHERE, 1 WHAT, 2 CLUE, 3 ANSWER; the count
static int hint_pages(int g, int *pg)
{
    int n = 0;
    const int gp = HN_ACT(g)->place;
    if (gp < HN_NPLACE && gp != T.pos) {
        pg[n++] = 0;
    }
    pg[n++] = 1;
    if (hn_clues(g)) {
        pg[n++] = 2;
    }
    if (HINTS[g].answer[0]) {
        pg[n++] = 3;
    }
    return n;
}
static void hint_page(int g, int page, char *h, int len)
{
    if (page == 0) {
        hint_where(HN_ACT(g)->place, h, len);
    } else if (page == 1) {
        hint_what(g, h, len);
    } else if (page == 2) {
        hint_clue(g, h, len);
    } else {
        snprintf(h, (size_t)len, "%s", HINTS[g].answer[s_lang]);
    }
}
// the text of the hint page at the tap level lvl (0: "Tap here for a hint."); *more: a next page exists
static void hint_text(int lvl, char *h, int len, const char *done_text, int *idx, int *count)
{
    const int g = next_goal();
    *idx = 0;
    *count = 0;
    if (g < 0) {
        snprintf(h, (size_t)len, "%s", done_text);
        return;
    }
    if (lvl <= 0) {
        snprintf(h, (size_t)len, "%s", TR("Tap here for a hint.", "הקישו כאן לרמז."));
        return;
    }
    int pg[4];
    const int n = hint_pages(g, pg);
    const int k = lvl > n ? n : lvl;
    hint_page(g, pg[k - 1], h, len);
    *idx = k;
    *count = n;
}
// draw a hint text in the box (cx, y0 .. ymax, width w): the text font, else the label font when it does not fit;
// more: a line "TAP FOR MORE" under the box (a next page of the hint exists)
static void hint_draw(const char *h, int cx, int y0, int ymax, int w, uint16_t col, bool more)
{
    if (more) {
        ui_text_c(&KF_LABEL, cx, ymax + 18, TR("TAP FOR MORE", "הקישו להמשך"), col);
    }
    static const int LH[2] = {20, 15};
    const kfont_t *f = &KF_TEXT;
    int lh = LH[0];
    s_wrap_dry = true;
    const int n = text_wrap(&KF_TEXT, cx, y0, w, h, col, LH[0]);
    s_wrap_dry = false;
    if (y0 + (n - 1) * LH[0] > ymax) {
        f = &KF_LABEL;
        lh = LH[1];
    }
    text_wrap(f, cx, y0, w, h, col, lh);
}

// a short line when the object in the hand fits the action a, but the action cannot act yet (the error sound alone
// does not tell the player why)
__attribute__((unused)) static void hint_use_fail(int a, char *h, int len)
{
    const uint64_t k = HN_ACT(a)->need & ((1ull << HN_NITEM) - 1) & ~T.bits;  // (the objects not in the bag)
    if (!HN_MODE_OK(HN_AMASK(HN_ACT(a)))) {
        snprintf(h, (size_t)len, "%s", TR("Not in this mode. The journal has a hint.", "לא במצב הזה. ביומן יש רמז."));
        return;
    }
    for (int i = 0; i < HN_NITEM; i++) {
        if (bit64(k, i)) {
            snprintf(h, (size_t)len, TR("You need the %s too.", "צריך גם את %s."), HN_ITEM(i));
            return;
        }
    }
    snprintf(h, (size_t)len, "%s", TR("Not yet. Something else comes first.", "עוד לא. משהו אחר בא קודם."));
}
