// The one copy of the quest fonts (kfont.h). keeper_font.inc is a made file with
// "static" data; here "static" is removed for that file, so that the five kfont_t are global (the quests use them).
#include "kfont.h"

#define static
#include "keeper_font.inc"
#undef static
