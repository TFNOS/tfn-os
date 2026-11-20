#pragma once
// BIDI: the order of the letters of one line of text with Hebrew for the screen, by the Unicode Bidirectional Algorithm
// (UAX #9) for one line with no explicit marks, and the mirror of the brackets in right-to-left text (rule L4). One
// header with static functions: the text engines of the firmware and of the apps include it (each app has its own copy:
// no ABI names).
//
// A line with a Hebrew letter is a right-to-left line (the paragraph level 1); a line with none keeps its order. In a
// right-to-left line a Latin word or a number keeps its own order, a phrase of Latin words stays one phrase, and a
// bracket, a quote or a colon goes to the side where it belongs. A bracket at a right-to-left level shows its mirror: a
// "(" at the start of a Hebrew group is drawn as ")" at the right of the group. The extra rule of TFN OS: "+" or "-"
// right before a digit at the start of a number (after a space, an opening bracket or the start of the line) stays with
// the number, so "-5" stays "-5"; after a letter it is a hyphen, as in a Hebrew prefix and a number.
//
// bidi_order(cp, n, ord, mir): ord[i] = the index in cp of the letter at the place i from the left of the line;
// mir[i] = 1 when that letter shows its mirror (bidi_mirror()). The result: true for a right-to-left line (else ord is
// 0 .. n - 1 and mir is all 0). Only the first BIDI_MAX letters of a longer line move (the rest keeps its order), and
// only the first BIDI_PAIRS pairs of brackets of a line are pairs.
// A combining mark (a Hebrew point) stays at its letter: in a right-to-left run it comes just before the letter, and a
// mark has no advance, so an engine that draws from the left puts it on the letter.
#include <stdbool.h>
#include <stdint.h>

#define BIDI_MAX   128  // (the stack of the caller: about 0.4 KB)
#define BIDI_PAIRS 16   // the pairs of brackets of a line (BD16 allows 63: a line of a small screen has fewer)

enum {
    BIDI_L,
    BIDI_R,
    BIDI_EN,
    BIDI_ES,
    BIDI_ET,
    BIDI_CS,
    BIDI_NSM,
    BIDI_WS,
    BIDI_ON,
    BIDI_AN,
    BIDI_AL
};

// the mirror of a letter (rule L4), or the letter itself
static inline uint16_t bidi_mirror(uint16_t c)
{
    switch (c) {
    case '(':
        return ')';
    case ')':
        return '(';
    case '[':
        return ']';
    case ']':
        return '[';
    case '{':
        return '}';
    case '}':
        return '{';
    case '<':
        return '>';
    case '>':
        return '<';
    case 0xAB:  // the guillemets
        return 0xBB;
    case 0xBB:
        return 0xAB;
    case 0x2039:
        return 0x203A;
    case 0x203A:
        return 0x2039;
    case 0x2264:  // less or equal, greater or equal
        return 0x2265;
    case 0x2265:
        return 0x2264;
    default:
        return c;
    }
}

static inline bool bidi_heb(uint16_t c)
{
    return c >= 0x590 && c <= 0x5FF;
}

// the bidirectional type of a letter (the classes of UAX #9 that the fonts of TFN OS can show)
static inline uint8_t bidi_type(uint16_t c)
{
    if (c >= '0' && c <= '9') {
        return BIDI_EN;
    }
    if (c == '+' || c == '-' || c == 0x2212) {
        return BIDI_ES;
    }
    if (c == '#' || c == '$' || c == '%' || c == 0xA2 || c == 0xA3 || c == 0xA5 || c == 0xB0 || c == 0x2030 ||
        c == 0x20AA || c == 0x20AC) {
        return BIDI_ET;
    }
    if (c == ',' || c == '.' || c == '/' || c == ':' || c == 0xA0) {
        return BIDI_CS;
    }
    if (c == ' ' || c == '\t') {
        return BIDI_WS;
    }
    if ((c >= 0x591 && c <= 0x5BD) || c == 0x5BF || c == 0x5C1 || c == 0x5C2 || c == 0x5C4 || c == 0x5C5 ||
        c == 0x5C7 || (c >= 0x64B && c <= 0x65F) || c == 0x670) {
        return BIDI_NSM;
    }
    if (bidi_heb(c) || (c >= 0xFB1D && c <= 0xFB4F) || c == 0x200F) {
        return BIDI_R;
    }
    if (c >= 0x660 && c <= 0x669) {
        return BIDI_AN;
    }
    if ((c >= 0x600 && c <= 0x6FF) || (c >= 0x750 && c <= 0x77F)) {
        return BIDI_AL;
    }
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= 0xC0 && c <= 0x24F && c != 0xD7 && c != 0xF7) ||
        (c >= 0x370 && c <= 0x3FF) || (c >= 0x400 && c <= 0x4FF) || c == 0xAA || c == 0xB5 || c == 0xBA ||
        c == 0x200E) {
        return BIDI_L;
    }
    return BIDI_ON;
}

// the strong direction of a resolved type for the rules N0 and N1 (a number acts as R): BIDI_L, BIDI_R or 0xFF
static inline uint8_t bidi_strong(uint8_t t)
{
    return t == BIDI_L ? BIDI_L : (t == BIDI_R || t == BIDI_EN || t == BIDI_AN) ? BIDI_R : 0xFF;
}

// the weak types (W1-W7) of the m letters of cp in t
static inline void bidi_weak(const uint16_t *cp, int m, uint8_t *t)
{
    // W1: a mark takes the type of the letter before it (at the start: R, the direction of the line)
    for (int i = 0; i < m; i++) {
        if (t[i] == BIDI_NSM) {
            t[i] = i ? t[i - 1] : BIDI_R;
        }
    }
    // W2, W3: a European number after an Arabic letter is an Arabic number; an Arabic letter is R
    bool after_al = false;
    for (int i = 0; i < m; i++) {
        if (t[i] == BIDI_L || t[i] == BIDI_R || t[i] == BIDI_AL) {
            after_al = t[i] == BIDI_AL;
        } else if (t[i] == BIDI_EN && after_al) {
            t[i] = BIDI_AN;
        }
        if (t[i] == BIDI_AL) {
            t[i] = BIDI_R;
        }
    }
    // (the extra rule of TFN OS: the sign at the start of a number belongs to the number)
    for (int i = 0; i + 1 < m; i++) {
        if (t[i] == BIDI_ES && t[i + 1] == BIDI_EN &&
            (i == 0 || t[i - 1] == BIDI_WS || cp[i - 1] == '(' || cp[i - 1] == '[' || cp[i - 1] == '{')) {
            t[i] = BIDI_EN;
        }
    }
    // W4: one separator between two numbers of one type
    for (int i = 1; i + 1 < m; i++) {
        if ((t[i] == BIDI_ES || t[i] == BIDI_CS) && t[i - 1] == BIDI_EN && t[i + 1] == BIDI_EN) {
            t[i] = BIDI_EN;
        } else if (t[i] == BIDI_CS && t[i - 1] == BIDI_AN && t[i + 1] == BIDI_AN) {
            t[i] = BIDI_AN;
        }
    }
    // W5: terminators next to a European number
    for (int i = 0; i < m;) {
        if (t[i] != BIDI_ET) {
            i++;
            continue;
        }
        int j = i;
        while (j < m && t[j] == BIDI_ET) {
            j++;
        }
        if ((i > 0 && t[i - 1] == BIDI_EN) || (j < m && t[j] == BIDI_EN)) {
            for (int k = i; k < j; k++) {
                t[k] = BIDI_EN;
            }
        }
        i = j;
    }
    // W6: the other separators and terminators are neutral; W7: a European number after a Latin letter acts as Latin
    uint8_t last = BIDI_R;
    for (int i = 0; i < m; i++) {
        if (t[i] == BIDI_ES || t[i] == BIDI_ET || t[i] == BIDI_CS) {
            t[i] = BIDI_ON;
        }
        if (t[i] == BIDI_L || t[i] == BIDI_R) {
            last = t[i];
        } else if (t[i] == BIDI_EN && last == BIDI_L) {
            t[i] = BIDI_L;
        }
    }
}

// N0: the pairs of brackets (BD16), in the order of their opening bracket, get the direction of their content
static inline void bidi_pairs(const uint16_t *cp, int m, uint8_t *t)
{
    int16_t st[BIDI_PAIRS], po[BIDI_PAIRS], pc[BIDI_PAIRS];
    uint16_t sc[BIDI_PAIRS];
    int ns = 0, np = 0;
    for (int i = 0; i < m && np < BIDI_PAIRS; i++) {
        if (t[i] != BIDI_ON) {
            continue;
        }
        const uint16_t c = cp[i];
        if (c == '(' || c == '[' || c == '{') {
            if (ns == BIDI_PAIRS) {
                break;
            }
            sc[ns] = bidi_mirror(c);
            st[ns++] = (int16_t)i;
        } else if (c == ')' || c == ']' || c == '}') {
            for (int d = ns - 1; d >= 0; d--) {
                if (sc[d] == c) {
                    // (the list stays in the order of the opening brackets: insert it in its place)
                    int k = np++;
                    while (k > 0 && po[k - 1] > st[d]) {
                        po[k] = po[k - 1];
                        pc[k] = pc[k - 1];
                        k--;
                    }
                    po[k] = st[d];
                    pc[k] = (int16_t)i;
                    ns = d;
                    break;
                }
            }
        }
    }
    for (int p = 0; p < np; p++) {
        bool in_r = false, in_l = false;
        for (int k = po[p] + 1; k < pc[p]; k++) {
            const uint8_t s = bidi_strong(t[k]);
            in_r = in_r || s == BIDI_R;
            in_l = in_l || s == BIDI_L;
        }
        uint8_t d;
        if (in_r) {
            d = BIDI_R;  // (the direction of the line)
        } else if (in_l) {
            d = BIDI_R;  // (the context before the pair decides: L only after a Latin letter)
            for (int k = po[p] - 1; k >= 0; k--) {
                const uint8_t s = bidi_strong(t[k]);
                if (s != 0xFF) {
                    d = s;
                    break;
                }
            }
        } else {
            continue;
        }
        t[po[p]] = t[pc[p]] = d;
        for (int b = 0; b < 2; b++) {  // (a mark after a bracket goes with it)
            for (int k = (b ? pc[p] : po[p]) + 1; k < m && bidi_type(cp[k]) == BIDI_NSM; k++) {
                t[k] = d;
            }
        }
    }
}

// N1, N2: a run of neutrals between two sides of one direction takes it, else the direction of the line
static inline void bidi_neutrals(int m, uint8_t *t)
{
    for (int i = 0; i < m;) {
        if (t[i] != BIDI_ON && t[i] != BIDI_WS) {
            i++;
            continue;
        }
        int j = i;
        while (j < m && (t[j] == BIDI_ON || t[j] == BIDI_WS)) {
            j++;
        }
        const uint8_t a = i > 0 ? bidi_strong(t[i - 1]) : BIDI_R, b = j < m ? bidi_strong(t[j]) : BIDI_R;
        const uint8_t d = a == b ? a : BIDI_R;
        for (int k = i; k < j; k++) {
            t[k] = d;
        }
        i = j;
    }
}

// I1, I2, L1, L2, L4: the levels (1 = right to left; 2 = Latin and numbers in it; the spaces at the end: 1), the
// order (each run of level 2 or more turns, then the whole line) and the mirror at level 1
static inline void bidi_reorder(const uint16_t *cp, int m, uint8_t *t, int16_t *ord, uint8_t *mir)
{
    uint8_t *lev = t;
    for (int i = 0; i < m; i++) {
        lev[i] = t[i] == BIDI_R ? 1 : 2;
    }
    for (int k = m - 1; k >= 0 && bidi_type(cp[k]) == BIDI_WS; k--) {
        lev[k] = 1;
    }
    for (int level = 2; level >= 1; level--) {
        for (int i = 0; i < m;) {
            if (lev[ord[i]] < level) {
                i++;
                continue;
            }
            int j = i;
            while (j < m && lev[ord[j]] >= level) {
                j++;
            }
            for (int a = i, b = j - 1; a < b; a++, b--) {
                const int16_t x = ord[a];
                ord[a] = ord[b];
                ord[b] = x;
            }
            i = j;
        }
    }
    for (int i = 0; i < m; i++) {
        mir[i] = lev[ord[i]] == 1 && bidi_mirror(cp[ord[i]]) != cp[ord[i]];
    }
}

static inline bool bidi_order(const uint16_t *cp, int n, int16_t *ord, uint8_t *mir)
{
    bool rtl = false;
    for (int i = 0; i < n; i++) {
        ord[i] = (int16_t)i;
        mir[i] = 0;
        rtl = rtl || bidi_heb(cp[i]);
    }
    if (!rtl) {
        return false;
    }
    const int m = n < BIDI_MAX ? n : BIDI_MAX;
    uint8_t t[BIDI_MAX];  // the type of each letter, at the end its level
    for (int i = 0; i < m; i++) {
        t[i] = bidi_type(cp[i]);
    }
    bidi_weak(cp, m, t);
    bidi_pairs(cp, m, t);
    bidi_neutrals(m, t);
    bidi_reorder(cp, m, t, ord, mir);
    return true;
}
