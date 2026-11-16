#pragma once
// COIL RIDER: the format of the levels (cr_levels.c is MADE by a script; do not edit it).
// The terrain of a level is a set of SHAPES. Each shape is one or more closed RINGS of vertices (an outer ring and its
// holes). The rule of the fill is even-odd for each shape: a point is solid when it is inside an odd number of the
// rings of one shape. The rings go round with the solid at the LEFT of each edge (so the air side of an edge
// (dx, dy) is its right normal (dy, -dx)). The rider moves in the air.
#include <stddef.h>
#include <stdint.h>

#define CR_VK 50.0f                     // the vertices are int16 in 1/50 m (2 cm)
enum { MAT_ROCK, MAT_BRICK, MAT_N };
enum { WORLD_DUSK, WORLD_NIGHT, WORLD_EMBER, WORLD_N };

typedef struct {
    const char *name[2];                // English, Hebrew
    uint8_t world;                      // the look (WORLD_*)
    uint16_t nv;
    const int16_t *v;                   // the vertices: x, y
    uint16_t nr;
    const uint16_t *r;                  // the rings: the first vertex, the count, the shape
    uint8_t ns;
    const uint8_t *smat;                // the material of each shape (MAT_*)
    uint8_t np;
    const int16_t *pk;                  // the pickups: x, y
    int16_t sx, sy;                     // the start: the ground point under the middle of the bike
    int8_t sdir;                        // the start direction (+1 = to the right)
    int16_t ex, ey;                     // the exit gate: its ground point
    int16_t box[4];                     // the play space: x0, y0, x1, y1
    uint16_t star[2];                   // the times of the stars (0.1 s): 3 stars <= star[0], 2 stars <= star[1]
    uint16_t ntrack;
    const uint8_t *track;               // the found track of the PC test (the demo of the title, the test key coil_auto)
} cr_level_t;
extern const cr_level_t CR_LEV[];
extern const int CR_NLEV;
