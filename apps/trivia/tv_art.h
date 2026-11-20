// the pictures of TRIVIA (made by apps/trivia/tools/mk_art.py; do not edit)
#pragma once
#include <stdint.h>
#define TV_ART_SIZES 4
#define TV_ART_NICON 27
#define TV_ART_NLOGO 3
static const uint8_t TV_ART_PX[TV_ART_SIZES] = {24, 36, 48, 72};
enum {
    TI_ISRAEL,
    TI_HISTORY,
    TI_BIBLE,
    TI_GEOGRAPHY,
    TI_SCIENCE,
    TI_NATURE,
    TI_SPORT,
    TI_MUSIC,
    TI_FILM,
    TI_LANGUAGE,
    TI_FOOD,
    TI_TECH,
    TI_TROPHY,
    TI_FIRE,
    TI_WATCH,
    TI_BULB,
    TI_BRAIN,
    TI_PARTY,
    TI_HAND,
    TI_OK,
    TI_NO,
    TI_STAR,
    TI_TWO,
    TI_PHONE,
    TI_THINK,
    TI_MEDAL,
    TI_CROWN,
};
extern const uint32_t TV_ART_RAW[TV_ART_SIZES + 1];
extern const uint32_t TV_ART_Z[TV_ART_SIZES + 1];
extern const uint8_t *const TV_ART_DATA[TV_ART_SIZES + 1];
