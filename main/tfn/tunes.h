#pragma once

// The start tunes of the games: each game has its own short tune (about 1 to 2 s).

typedef enum {
    TUNE_RUN,
    TUNE_HOP,
    TUNE_KNIGHT,
    TUNE_DRIVE,
    TUNE_PONG,
    TUNE_STACK,
    TUNE_PRISM,
    TUNE_TILT,
    TUNE_SPLAT,
    TUNE_SHAKE,
    TUNE_RALLY,
    TUNE_ZAP,
    TUNE_ECHO,
    TUNE_WOBBLE,
    TUNE_KABOOM,
    TUNE_WHACK,
    TUNE_TILES,
    TUNE_MERGE,
    TUNE_BUBBLE,
    TUNE_BURGER,
    TUNE_PINBALL,
    TUNE_SAFE,
    TUNE_POUR,
    TUNE_EGG,
    TUNE_MORSE,
    TUNE_BRASS,
    TUNE_TENNIS,
    TUNE_BITS,
    TUNE_SIGHT,
    TUNE_SKY,
    TUNE_OHM,
    TUNE_ROPE,
    TUNE_CRUISE,
    TUNE_FIGHT,
    TUNE_QUEST,
    TUNE_FIGHT_GO,
    TUNE_FIGHT_KO,
    TUNE_FIGHT_WIN,
    TUNE_FIGHT_LOSE,
    TUNE_GRAINS,
    TUNE_WRONG,
    TUNE_FIGHT_VS,  // FIGHT: the VS screen
    TUNE_PIXEL_GO,  // PIXEL: a game or a review starts
    TUNE_WRONG_GO,  // WRONG ROOM: a level starts
    TUNE_COUNT,
} tune_t;

// Play the start tune of a game. A new tune stops the old one. Safe from any task.
void tune_play(tune_t t);

// The length of a tune in ms (the end of its last note).
int tune_ms(tune_t t);
