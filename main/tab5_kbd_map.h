/**
 * @file tab5_kbd_map.h
 * @brief The key matrix of the Tab5 Keyboard to PC keys (pure C, no hardware).
 *
 * The matrix tables (the HID usage and the Shift that the keycap needs, for
 * each of the 5 x 14 keys, without and with Sym) are from the M5Stack library
 * M5Unit-KEYBOARD, src/unit/unit_Tab5Keyboard.cpp (MIT licence, (c) 2026
 * M5Stack Technology CO LTD), "captured from device HID-mode logs".
 *
 * The keys, row by row (the base layer):
 *   0: Esc 1 2 3 4 5 6 7 8 9 0 - + Del
 *   1: ` ! @ # $ % ^ & * ( ) [ ] \
 *   2: Tab q w e r t y u i o p ; ' Backspace
 *   3: Sym Aa a s d f g h j k l Up _ Enter
 *   4: Ctrl Alt z x c v b n m . Left Down Right Space
 *
 * tests/host/test_kbd_map.c checks this file on the PC.
 */
#ifndef TAB5_KBD_MAP_H
#define TAB5_KBD_MAP_H

#include <stdbool.h>
#include <stdint.h>

#define TAB5_KBD_ROWS 5
#define TAB5_KBD_COLS 14

/* The four modifier keys of the matrix (row, col). */
#define TAB5_KBD_IS_SYM(r, c)  ((r) == 3 && (c) == 0)
#define TAB5_KBD_IS_AA(r, c)   ((r) == 3 && (c) == 1)
#define TAB5_KBD_IS_CTRL(r, c) ((r) == 4 && (c) == 0)
#define TAB5_KBD_IS_ALT(r, c)  ((r) == 4 && (c) == 1)

/* {HID usage, HID modifier}: 0x02 = the keycap character needs Shift. */
static const uint8_t TAB5_KBD_BASE[TAB5_KBD_ROWS * TAB5_KBD_COLS][2] = {
        {0x29, 0}, {0x1E, 0}, {0x1F, 0}, {0x20, 0}, {0x21, 0}, {0x22, 0}, {0x23, 0}, {0x24, 0}, {0x25, 0}, {0x26, 0},
        {0x27, 0}, {0x2D, 0}, {0x2E, 2}, {0x4C, 0}, {0x35, 0}, {0x1E, 2}, {0x1F, 2}, {0x20, 2}, {0x21, 2}, {0x22, 2},
        {0x23, 2}, {0x24, 2}, {0x25, 2}, {0x26, 2}, {0x27, 2}, {0x2F, 0}, {0x30, 0}, {0x31, 0}, {0x2B, 0}, {0x14, 0},
        {0x1A, 0}, {0x08, 0}, {0x15, 0}, {0x17, 0}, {0x1C, 0}, {0x18, 0}, {0x0C, 0}, {0x12, 0}, {0x13, 0}, {0x33, 0},
        {0x34, 0}, {0x2A, 0}, {0x00, 0}, {0x00, 0}, {0x04, 0}, {0x16, 0}, {0x07, 0}, {0x09, 0}, {0x0A, 0}, {0x0B, 0},
        {0x0D, 0}, {0x0E, 0}, {0x0F, 0}, {0x52, 0}, {0x2D, 2}, {0x28, 0}, {0x00, 0}, {0x00, 0}, {0x1D, 0}, {0x1B, 0},
        {0x06, 0}, {0x19, 0}, {0x05, 0}, {0x11, 0}, {0x10, 0}, {0x37, 0}, {0x50, 0}, {0x51, 0}, {0x4F, 0}, {0x2C, 0},
};

/* The Sym layer of the library: only these keys differ from the base layer. */
static inline bool tab5_kbd_sym_hid(uint8_t row, uint8_t col, uint8_t *hid, uint8_t *mod)
{
    static const uint8_t k_sym[][4] = {
            /* row, col, usage, modifier */
            {1, 0, 0x35, 2},  /* ` -> ~ */
            {1, 1, 0x38, 2},  /* ! -> ? */
            {1, 8, 0x38, 0},  /* * -> / */
            {1, 9, 0x36, 2},  /* ( -> < */
            {1, 10, 0x37, 2}, /* ) -> > */
            {1, 11, 0x2F, 2}, /* [ -> { */
            {1, 12, 0x30, 2}, /* ] -> } */
            {1, 13, 0x31, 2}, /* \ -> | */
            {2, 11, 0x33, 2}, /* ; -> : */
            {2, 12, 0x34, 2}, /* ' -> " */
            {3, 12, 0x2E, 0}, /* _ -> = */
            {4, 9, 0x36, 0},  /* . -> , */
    };
    for (unsigned i = 0; i < sizeof(k_sym) / sizeof(k_sym[0]); i++) {
        if (k_sym[i][0] == row && k_sym[i][1] == col) {
            *hid = k_sym[i][2];
            *mod = k_sym[i][3];
            return true;
        }
    }
    return false;
}

/** A HID usage to the PC set-1 make code. ext: the code has the E0 prefix. */
static inline bool tab5_kbd_hid_to_pc(uint8_t hid, uint8_t *sc, uint8_t *ext)
{
    static const uint8_t k_letters[26] = {
            0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32,
            0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C,
    };
    *ext = 0;
    if (hid >= 0x04 && hid <= 0x1D) {
        *sc = k_letters[hid - 0x04];
        return true;
    }
    if (hid >= 0x1E && hid <= 0x27) {
        *sc = (uint8_t)(0x02 + (hid - 0x1E)); /* 1 .. 9, 0 */
        return true;
    }
    switch (hid) {
    case 0x28:
        *sc = 0x1C;
        return true; /* Enter */
    case 0x29:
        *sc = 0x01;
        return true; /* Esc */
    case 0x2A:
        *sc = 0x0E;
        return true; /* Backspace */
    case 0x2B:
        *sc = 0x0F;
        return true; /* Tab */
    case 0x2C:
        *sc = 0x39;
        return true; /* Space */
    case 0x2D:
        *sc = 0x0C;
        return true; /* - _ */
    case 0x2E:
        *sc = 0x0D;
        return true; /* = + */
    case 0x2F:
        *sc = 0x1A;
        return true; /* [ { */
    case 0x30:
        *sc = 0x1B;
        return true; /* ] } */
    case 0x31:
        *sc = 0x2B;
        return true; /* \ | */
    case 0x33:
        *sc = 0x27;
        return true; /* ; : */
    case 0x34:
        *sc = 0x28;
        return true; /* ' " */
    case 0x35:
        *sc = 0x29;
        return true; /* ` ~ */
    case 0x36:
        *sc = 0x33;
        return true; /* , < */
    case 0x37:
        *sc = 0x34;
        return true; /* . > */
    case 0x38:
        *sc = 0x35;
        return true; /* / ? */
    case 0x4C:
        *sc = 0x53;
        *ext = 1;
        return true; /* Delete */
    case 0x4F:
        *sc = 0x4D;
        *ext = 1;
        return true; /* Right */
    case 0x50:
        *sc = 0x4B;
        *ext = 1;
        return true; /* Left */
    case 0x51:
        *sc = 0x50;
        *ext = 1;
        return true; /* Down */
    case 0x52:
        *sc = 0x48;
        *ext = 1;
        return true; /* Up */
    default:
        return false;
    }
}

typedef struct {
    uint8_t sc;    /* set-1 make code */
    uint8_t ext;   /* E0 prefix */
    uint8_t shift; /* the keycap character needs Shift */
} tab5_pc_key_t;

/**
 * The PC key of a matrix key that is not a modifier key, with or without Sym
 * held. False for a modifier key or a key with no PC key.
 */
static inline bool tab5_kbd_map(uint8_t row, uint8_t col, bool sym, tab5_pc_key_t *out)
{
    if (row >= TAB5_KBD_ROWS || col >= TAB5_KBD_COLS) {
        return false;
    }
    out->ext = 0;
    out->shift = 0;
    if (sym) {
        /* The PC keys that the keyboard does not have. */
        static const uint8_t k_f[12] = {0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42, 0x43, 0x44, 0x57, 0x58};
        if (row == 0 && col >= 1 && col <= 12) {
            out->sc = k_f[col - 1]; /* F1 .. F12 */
            return true;
        }
        if (row == 0 && col == 13) {
            out->sc = 0x52;
            out->ext = 1;
            return true;
        } /* Insert */
        if (row == 3 && col == 11) {
            out->sc = 0x49;
            out->ext = 1;
            return true;
        } /* PgUp */
        if (row == 4 && col == 11) {
            out->sc = 0x51;
            out->ext = 1;
            return true;
        } /* PgDn */
        if (row == 4 && col == 10) {
            out->sc = 0x47;
            out->ext = 1;
            return true;
        } /* Home */
        if (row == 4 && col == 12) {
            out->sc = 0x4F;
            out->ext = 1;
            return true;
        } /* End */
    }
    const int i = row * TAB5_KBD_COLS + col;
    uint8_t hid = TAB5_KBD_BASE[i][0];
    uint8_t mod = TAB5_KBD_BASE[i][1];
    if (sym) {
        (void)tab5_kbd_sym_hid(row, col, &hid, &mod);
    }
    if (hid == 0 || !tab5_kbd_hid_to_pc(hid, &out->sc, &out->ext)) {
        return false;
    }
    out->shift = (mod & 0x22) ? 1 : 0;
    return true;
}

#endif /* TAB5_KBD_MAP_H */
