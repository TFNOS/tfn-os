// INTEGRALS: the contents (a card for each chapter) and the page of a chapter (a tile for each lesson).

#define MAX_CH  24
#define MAX_LES 10
static ttext_t s_tocT[MAX_CH];  // the names of the chapters on the cards
static tform_t s_tocF[MAX_CH];  // the formulas of the cards
static ttext_t s_chT[MAX_LES];  // the titles of the lessons
static tform_t s_chF[MAX_LES];  // the formulas of the lessons
static ttext_t s_chName, s_chWhen;

static const char *SEC_EN[4] = {"THE BASICS", "THE TECHNIQUES", "THE TRICKS", "THE CLASSICS"};
static const char *SEC_HE[4] = {"היסודות", "השיטות", "הטריקים", "הקלאסיקות"};

// ---------------------------------------------------------------- the contents
static void toc_layout(void)
{
    const int L = lay();
    rc_t view;
    int cw, chh, gap, cols;
    int tw = 0;  // the width of the text of a card (the round frame: the middle part of a card)
    if (rnd()) {
        view = RC(0, RS(100), gfx_w, RS(306));
        cw = RS(380), chh = RS(110), gap = RS(10), cols = 1, tw = RS(290);
    } else if (L == LY_BAR) {
        view = RC(8, 36, gfx_w - 16, 128);
        cw = 172, chh = 128, gap = 10, cols = 0;
    } else if (L == LY_TAB) {
        view = RC(6, 56, gfx_w - 12, gfx_h - 60);
        cw = 202, chh = 140, gap = 10, cols = 3;
    } else {
        view = RC(4, 40, gfx_w - 8, gfx_h - 42);
        cw = 150, chh = 104, gap = 6, cols = 2;
    }
    if (!tw) {
        tw = cw;
    }
    const float keep = s_gT.n == s_nch ? ks_pos(&s_gT.ks) : 0;
    grid_init(&s_gT, view, cw, chh, gap, cols, s_nch);
    ks_jump(&s_gT.ks, keep);
    s_ar = s_arT;
    s_ar->n = 0;
    s_mbuf_n = 0;
    for (int i = 0; i < s_nch && i < MAX_CH; i++) {
        tt_layout(&s_tocT[i], L_(s_ch[i].en, s_ch[i].he), L == LY_TAB ? 1 : 2, tw - 30, 44, s_he);
        t_layout(S_(s_ch[i].icon), L == LY_TAB ? 1 : 3, 4, TS_T, tw - 16, chh - 64, &s_tocF[i], false);
    }
}
static void toc_card(int i, rc_t r, int alpha)
{
    const int sec = sec_of(i);
    const uint16_t hue = sec_col(sec, 255);
    tfd_rrect(r.x, r.y, r.w, r.h, 14, C_CARD, fa(alpha));
    tfd_rrect(r.x, r.y, r.w, 5, 2, hue, fa(alpha * 200 / 255));
    if (rnd()) {
        r.x += (r.w - RS(290)) / 2;  // the text in the middle part of the round card
        r.w = RS(290);
    }
    char num[8];
    snprintf(num, sizeof num, "%d", i + 1);
    // the number at the end corner, the name over the full width at the start side
    const int nx = s_he ? r.x + 12 : r.x + r.w - 12;
    kf_line_c(&KF_TEXT, nx, r.y + 24, num, ui_mix(hue, C_CARD, 90), fa(alpha));
    const ttext_t *T = &s_tocT[i];
    const int tx = s_he ? r.x + 18 : r.x + 8;
    tt_draw(s_arT, T, tx, r.y + 9 + (T->lines == 1 ? 9 : 0), C_INK, alpha);
    const tform_t *F = &s_tocF[i];
    const uint16_t keep = s_tcol[TC_INK];
    s_tcol[TC_INK] = ui_mix(hue, C_INK, 150);
    const int fy = r.y + 54 + (r.h - 54 - 14 - F->h) / 2;
    t_draw(s_arT, F, r.x + (r.w - F->w) / 2, fy, alpha, 0);
    s_tcol[TC_INK] = keep;
    // the lessons: a dot each, full when seen
    const int nl = s_ch[i].n;
    for (int k = 0; k < nl; k++) {
        const int dx = (k - (nl - 1) / 2.0f) * 10;
        const bool on = seen(i, k);
        tfd_disc(r.x + r.w / 2 + dx, r.y + r.h - 9, on ? 3.0f : 2.4f, on ? hue : C_LINE, fa(alpha));
    }
}
// the head of a list in the round frame: the name in the middle of the top cap, a label under it
static void round_head(const char *name, const char *label, uint16_t lcol, int alpha)
{
    line_fit(name, gfx_w / 2 - RS(110), RS(70), RS(220), &KF_BIG, C_INK, alpha, 0);
    if (label) {
        line_fit(label, gfx_w / 2 - RS(130), RS(94), RS(260), &KF_LABEL, lcol, alpha, 0);
    }
}
static void toc_render(int alpha)
{
    back_draw();
    const int L = lay();
    if (rnd()) {
        const int first = (int)(ks_pos(&s_gT.ks) / (s_gT.ch + s_gT.gap) + 0.5f);
        const int sec = sec_of(first < s_nch ? first : 0);
        round_head(s_he ? "תוכן העניינים" : "CONTENTS", s_he ? SEC_HE[sec] : SEC_EN[sec], sec_col(sec, 255), alpha);
        grid_clip(&s_gT);
        for (int i = 0; i < s_nch && i < MAX_CH; i++) {
            const rc_t r = grid_card(&s_gT, i);
            if (r.y + r.h >= s_gT.view.y && r.y <= s_gT.view.y + s_gT.view.h) {
                toc_card(i, r, alpha);
            }
        }
        clip_off();
        return;
    }
    const rc_t hr = mir(RC(L == LY_TAB ? 80 : 58, 0, gfx_w - 140, 1));
    line_start(s_he ? "תוכן העניינים" : "CONTENTS", hr.x, L == LY_TAB ? 36 : 27, hr.w, L == LY_BAR ? &KF_TEXT : &KF_BIG,
               C_INK, alpha);
    grid_clip(&s_gT);
    for (int i = 0; i < s_nch && i < MAX_CH; i++) {
        const rc_t r = grid_card(&s_gT, i);
        if (r.x + r.w < 0 || r.x > gfx_w || r.y + r.h < s_gT.view.y || r.y > gfx_h) {
            continue;
        }
        toc_card(i, r, alpha);
    }
    clip_off();
    grid_bar(&s_gT, alpha);
    // the name of the section of the first card in the view (the bar) or at the top (the others)
    const int first = (int)(ks_pos(&s_gT.ks) / (L == LY_BAR ? s_gT.cw + s_gT.gap : 1e9f));
    const int sec = sec_of(first < s_nch ? first : 0);
    const rc_t sr = mir(RC(gfx_w - 230, 0, 220, 1));
    line_fit(s_he ? SEC_HE[sec] : SEC_EN[sec], sr.x, L == LY_TAB ? 34 : 25, sr.w, &KF_LABEL, sec_col(sec, 255), alpha,
             s_he ? -1 : 1);
}

// ---------------------------------------------------------------- the page of a chapter
static rc_t s_chPanel;
static void chap_layout(void)
{
    const int L = lay();
    const int n = s_ch[s_cc].n;
    rc_t view;
    int cw, chh, gap, cols;
    if (rnd()) {
        s_chPanel = RC(gfx_w / 2 - RS(155), RS(98), RS(310), RS(70));
        view = RC(0, RS(176), gfx_w, RS(220));
        cw = RS(390), chh = RS(80), gap = RS(8), cols = 1;
    } else if (L == LY_BAR) {
        s_chPanel = RC(0, 40, 236, 132);
        view = RC(244, 8, gfx_w - 252, 156);
        cw = 178, chh = 156, gap = 10, cols = 0;
    } else if (L == LY_TAB) {
        s_chPanel = RC(0, 60, 250, 300);
        view = RC(258, 8, gfx_w - 264, gfx_h - 12);
        cw = 182, chh = 150, gap = 8, cols = 2;
    } else {
        s_chPanel = RC(0, 44, gfx_w, 50);
        view = RC(4, 98, gfx_w - 8, gfx_h - 100);
        cw = gfx_w - 8, chh = 62, gap = 6, cols = 1;
    }
    grid_init(&s_gC, view, cw, chh, gap, cols, n);
    s_ar = s_arC;
    s_ar->n = 0;
    s_mbuf_n = 0;
    const igd_chapter_t *c = &s_ch[s_cc];
    const bool box = L == LY_BOX || rnd();
    const int tw = rnd() ? RS(330) : cw;  // (the round frame: the text of a tile in its middle part)
    if (rnd()) {
        tt_layout(&s_chWhen, L_(c->when_en, c->when_he), 2, RS(310), RS(70), s_he);
    } else if (L == LY_BOX) {
        tt_layout(&s_chName, L_(c->en, c->he), 0, gfx_w - 64, 30, s_he);
        tt_layout(&s_chWhen, L_(c->when_en, c->when_he), 2, gfx_w - 16, 44, s_he);
    } else if (L == LY_BAR) {
        tt_layout(&s_chName, L_(c->en, c->he), 0, s_chPanel.w - 70, 40, s_he);
        tt_layout(&s_chWhen, L_(c->when_en, c->when_he), 2, s_chPanel.w - 24, 118, s_he);
    } else {
        tt_layout(&s_chName, L_(c->en, c->he), 0, s_chPanel.w - 24, 70, s_he);
        tt_layout(&s_chWhen, L_(c->when_en, c->when_he), 1, s_chPanel.w - 24, 180, s_he);
    }
    for (int i = 0; i < n && i < MAX_LES; i++) {
        const igd_lesson_t *l = les_at(s_cc, i);
        if (box) {
            const int nw = rnd() ? tw * 45 / 100 : tw / 2;  // the part of the name (round: 45 %)
            tt_layout(&s_chT[i], L_(l->en, l->he), 2, nw - 16, 40, s_he);
            t_layout(S_(l->head), 2, 4, TS_T, tw - nw - 8, chh - 10, &s_chF[i], false);
        } else {
            tt_layout(&s_chT[i], L_(l->en, l->he), 2, cw - 20, 40, s_he);
            t_layout(S_(l->head), 2, 4, TS_T, cw - 16, chh - 78, &s_chF[i], false);
        }
    }
}
static const char *kind_name(int li, char *buf, int n)
{
    const igd_lesson_t *l = les_at(s_cc, li);
    if (l->kind == LK_IDEA) {
        return s_he ? "הרעיון" : "THE IDEA";
    }
    if (l->kind == LK_GEM) {
        return s_he ? "קלאסיקה" : "A CLASSIC";
    }
    int k = 0;
    for (int i = 0; i <= li; i++) {
        k += les_at(s_cc, i)->kind == LK_EX;
    }
    snprintf(buf, n, s_he ? "דוגמה %d" : "EXAMPLE %d", k);
    return buf;
}
static void check_mark(float x, float y, float s, uint16_t c, int alpha)
{
    tfd_line(x, y, x + s * 0.4f, y + s * 0.4f, 2.2f, c, fa(alpha));
    tfd_line(x + s * 0.4f, y + s * 0.4f, x + s, y - s * 0.5f, 2.2f, c, fa(alpha));
}
static void chap_tile(int i, rc_t r, int alpha)
{
    const int sec = sec_of(s_cc);
    const uint16_t hue = sec_col(sec, 255);
    const bool box = lay() == LY_BOX || rnd();
    const bool here = i == s_li;
    tfd_rrect(r.x, r.y, r.w, r.h, 14, here ? ui_mix(C_CARD, hue, 40) : C_CARD, fa(alpha));
    if (rnd()) {
        r.x += (r.w - RS(330)) / 2;  // the text in the middle part of the round tile
        r.w = RS(330);
    }
    char kb[24];
    const char *kn = kind_name(i, kb, sizeof kb);
    const int half = box ? (rnd() ? r.w * 45 / 100 : r.w / 2) : r.w;  // (round: a wider part for the formula)
    const rc_t kr = s_he ? RC(r.x + r.w - half + 10, r.y, half - 20, 1) : RC(r.x + 10, r.y, half - 20, 1);
    line_start(kn, kr.x, r.y + 19, kr.w, &KF_LABEL, hue, alpha);
    tt_draw(s_arC, &s_chT[i], s_he ? r.x + r.w - half + 8 : r.x + 10, r.y + 22, C_INK, alpha);
    const tform_t *F = &s_chF[i];
    const uint16_t keep = s_tcol[TC_INK];
    s_tcol[TC_INK] = ui_mix(hue, C_INK, 170);
    if (box) {
        const int fw = r.w - half;
        const int fx0 = s_he ? r.x + 4 : r.x + half;
        t_draw(s_arC, F, fx0 + (fw - F->w) / 2, r.y + (r.h - F->h) / 2, alpha, 0);
    } else {
        t_draw(s_arC, F, r.x + (r.w - F->w) / 2, r.y + 70 + (r.h - 70 - F->h) / 2, alpha, 0);
    }
    s_tcol[TC_INK] = keep;
    if (seen(s_cc, i)) {
        const float cx = s_he ? r.x + 12.0f : r.x + r.w - 24.0f;
        check_mark(cx, r.y + 14.0f, 12, hue, alpha);
    }
}
static void chap_render(int alpha)
{
    back_draw();
    const int L = lay();
    const rc_t p = mir(s_chPanel);
    if (rnd()) {
        const igd_chapter_t *c = &s_ch[s_cc];
        const kfont_t *const faces[] = {&KF_BIG, &KF_TEXT, &KF_NAME};
        const char *nm = L_(c->en, c->he);
        line_fit(nm, gfx_w / 2 - RS(105), RS(78), RS(210), fit_face(nm, RS(210), faces, 3), C_INK, alpha, 0);
        tt_draw(s_arC, &s_chWhen, p.x, p.y, C_TXT, alpha);
    } else if (L == LY_BOX) {
        tt_draw(s_arC, &s_chName, s_he ? 8 : 56, 2, C_INK, alpha);
        tt_draw(s_arC, &s_chWhen, 8, 44, C_TXT, alpha);
    } else if (L == LY_BAR) {
        tt_draw(s_arC, &s_chName, s_he ? p.x + 8 : 62, 4, C_INK, alpha);
        tt_draw(s_arC, &s_chWhen, p.x + 12, 46, C_TXT, alpha);
    } else {
        tt_draw(s_arC, &s_chName, p.x + 12, p.y - 4, C_INK, alpha);
        tt_draw(s_arC, &s_chWhen, p.x + 12, p.y + s_chName.h + 10, C_TXT, alpha);
    }
    grid_clip(&s_gC);
    for (int i = 0; i < s_gC.n && i < MAX_LES; i++) {
        const rc_t r = grid_card(&s_gC, i);
        if (r.x + r.w < 0 || r.x > gfx_w || r.y + r.h < s_gC.view.y || r.y > gfx_h) {
            continue;
        }
        chap_tile(i, r, alpha);
    }
    clip_off();
    grid_bar(&s_gC, alpha);
}
