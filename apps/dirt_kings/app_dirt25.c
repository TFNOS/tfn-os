// DIRT KINGS (GAMES): the view "more like Super Off Road": the arena
// does not turn, it shows at an angle from the front; after the kick-off the camera zooms in on the player's truck; a
// small map in a corner. The code of app_dirt.c with DK_FIXED and its own data (dk25_data.c) and saves. (app_dirt.c
// alone is the first view, where the arena turns; it is not in the list of the apps.)
#define DK_FIXED 1
#define DK_SYM   app_dirt25
#define DK_NAME  "DIRT KINGS"
#define DK_KEY   "d5_"
#include "app_dirt.c"
