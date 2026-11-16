// FOUR SEASONS: the geometry of the scenes (MADE by tools/bake.py from the sc_*.py files; do not edit).

enum {
    SC_VALLEY,
    SC_FARM,
    SC_POND,
    SC_TREE,
    SC_SWING,
    SC_VINES,
    SC_SHED,
    SC_SHORE,
    SC_ISLAND,
    SC_ATTIC,
    SC_WELL,
    SC_HIVE,
    SC_MAILBOX,
    SC_GARDEN,
    SC_N
};
enum {
    HS_VALLEY_TREE,
    HS_VALLEY_KITE,
    HS_VALLEY_SWING,
    HS_VALLEY_VINES,
    HS_VALLEY_SHED,
    HS_VALLEY_POND,
    HS_VALLEY_FARM,
    HS_VALLEY_HILL,
    HS_FARM_MAILBOX,
    HS_FARM_HOUSE,
    HS_FARM_WELL,
    HS_FARM_HIVE,
    HS_FARM_GARDEN,
    HS_POND_SHORE,
    HS_POND_ISLAND,
    HS_POND_LOG,
    HS_TREE_KITE,
    HS_TREE_TRUNK,
    HS_SWING_SQUIRREL,
    HS_SWING_SWING,
    HS_VINES_KEY,
    HS_VINES_VINES,
    HS_SHED_DOOR,
    HS_SHED_BENCH,
    HS_SHED_POLE,
    HS_SHORE_SLUICE,
    HS_ISLAND_RIBBON,
    HS_ISLAND_WILLOW,
    HS_ATTIC_TRUNK,
    HS_ATTIC_WINDOW,
    HS_WELL_HANDLE,
    HS_WELL_WELL,
    HS_HIVE_HIVE,
    HS_MAILBOX_MAILBOX,
    HS_GARDEN_BED,
    HS_N
};
typedef struct { uint8_t scene; int16_t x, y, w, h; } hot_t;
static const hot_t HOT[HS_N] = {
    {SC_VALLEY, 215, 30, 60, 64},   // tree
    {SC_VALLEY, 249, 22, 28, 28},   // kite
    {SC_VALLEY, 275, 58, 28, 36},   // swing
    {SC_VALLEY, 50, 92, 150, 60},   // vines
    {SC_VALLEY, 510, 96, 76, 46},   // shed
    {SC_VALLEY, 340, 140, 140, 26},   // pond
    {SC_VALLEY, 620, 112, 60, 44},   // farm
    {SC_VALLEY, 185, 60, 160, 40},   // hill
    {SC_FARM, 152, 98, 52, 58},   // mailbox
    {SC_FARM, 214, 40, 162, 114},   // house
    {SC_FARM, 398, 82, 66, 70},   // well
    {SC_FARM, 474, 98, 96, 54},   // hive
    {SC_FARM, 584, 104, 92, 60},   // garden
    {SC_POND, 560, 40, 80, 62},   // shore
    {SC_POND, 420, 34, 116, 86},   // island
    {SC_POND, 232, 118, 176, 54},   // log
    {SC_TREE, 426, 26, 88, 96},   // kite
    {SC_TREE, 150, 112, 100, 56},   // trunk
    {SC_SWING, 108, 40, 104, 86},   // squirrel
    {SC_SWING, 388, 60, 90, 104},   // swing
    {SC_VINES, 368, 40, 64, 84},   // key
    {SC_VINES, 150, 44, 160, 100},   // vines
    {SC_SHED, 190, 70, 92, 98},   // door
    {SC_SHED, 322, 98, 72, 44},   // bench
    {SC_SHED, 398, 72, 54, 70},   // pole
    {SC_SHORE, 274, 30, 188, 110},   // sluice
    {SC_ISLAND, 402, 37, 84, 76},   // ribbon
    {SC_ISLAND, 272, 96, 82, 62},   // willow
    {SC_ATTIC, 138, 84, 168, 84},   // trunk
    {SC_ATTIC, 364, 28, 88, 78},   // window
    {SC_WELL, 234, 64, 132, 66},   // handle
    {SC_WELL, 382, 30, 92, 100},   // well
    {SC_HIVE, 176, 38, 236, 132},   // hive
    {SC_MAILBOX, 184, 30, 276, 124},   // mailbox
    {SC_GARDEN, 398, 30, 112, 136},   // bed
};
enum {
    AN_N
};
static const int16_t ANCH[AN_N + 1][2] = {
    {0, 0}
};
#define V_VALLEY_KITE 0
#define V_VALLEY_POLE 1
#define V_VALLEY_LOG 2
#define V_POND_LOG 0
#define V_TREE_KITE 0
#define V_SWING_SQUIRREL 0
#define V_SWING_STRING 1
#define V_VINES_KEY 0
#define V_SHED_DOOR 0
#define V_SHED_POLE 1
#define V_SHORE_SLUICE 0
#define V_ISLAND_RIBBON 0
#define V_ATTIC_TRUNK 0
#define V_WELL_HANDLE 0
#define V_HIVE_NEST 0
#define V_MAILBOX_SEEDS 0
#define V_GARDEN_PLANT 0
typedef struct { const char *name; int16_t w, h; uint8_t nvar; } scene_t;
static const scene_t SCENE[SC_N] = {
    {"valley", 720, 184, 3},
    {"farm", 720, 184, 0},
    {"pond", 720, 184, 1},
    {"tree", 672, 180, 1},
    {"swing", 672, 180, 2},
    {"vines", 672, 180, 1},
    {"shed", 672, 180, 2},
    {"shore", 672, 180, 1},
    {"island", 672, 180, 1},
    {"attic", 672, 180, 1},
    {"well", 672, 180, 1},
    {"hive", 672, 180, 1},
    {"mailbox", 672, 180, 1},
    {"garden", 672, 180, 1},
};
