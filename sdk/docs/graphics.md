# TFN OS graphics guide: the ways to make the pictures of a game

The games of this repo use nine ways (classes A to I) to make their pictures. The early games draw everything with C
code in each frame. The newest games show pictures that Blender renders on the PC, and the board draws only the things
that move, in 3D, over them. This guide puts each game in its class, tells how each class works, which tools make its
art, what it costs on the board, and which class to use for a new game.

Read [visual.md](visual.md) first (the screens, the frames, the fonts, the touch). The speed and memory rules are in
[checklist.md](checklist.md) section 5. The section of each game in `MANUAL.md` has its details. The steps to make a
game of each class are in [howto.md](howto.md) section 3.

Note: some art tools of the games are not in this repo. The text names the method, so that you can make your own
tool. A tool that a game needs for a rebuild of its data goes into `apps/<app>/tools/` (in git).

## 1. The classes at a glance

| Class | The pictures come from | Drawn on the board | Data | Examples |
| --- | --- | --- | --- | --- |
| A. Code-drawn 2D | C code in each frame | all | none | PONG, TILES, SPLAT, RUN, MURANO, ORBITAL, GLASSHOUSE, SUMMER CUP, COIL RIDER, JELLY COURIER, TRIVIA |
| B. Cartoon puppets | C code (`libs/toon`) | all | none | FIGHT |
| C. Code-drawn quest scenes | shapes in C (`*_art.inc`) | all, the still layer cached | none | KEEPER, ROOM, SIDE B |
| D. Offline art generators | Python or JS art code (pixel art, canvas) | blits of sprites and tiles | in the app | CRUISE, SCENES, GUMMY SIEGE |
| E. Pre-rendered pictures, 2D play | three.js or Blender renders | pictures + sprites | in the app | WHITE CITY, CHESS, STORM WATCH |
| F. 2.5D: Blender sprites on a drawn world | Blender sprites, textures | the world by code, scaled sprites | in the app | RIVIERA GP, DIRT KINGS, ZIKI RUSH |
| G. Real-time 3D polygons | meshes in C or made by code | all (triangles) | none | IRON ARENA, BEEPER 97, CUBBY, INK KARTS |
| H. Painted places (SD pack) | an offline painter on the PC | the picture + layers + 2D animation | SD card | THE TURNING WOOD, AFTER HOURS |
| I. Blender stage + live 3D actors | Blender Cycles (colour, depth, tap map) | the picture + 3D actors with a z-buffer | SD card | AFTER HOURS 3D, NIGHT TAPES |

Classes A to C and G need no art pipeline (the code makes the look). Classes D to F bake art into the app file.
Classes H and I keep the art in a data pack on the SD card (the space on the card is not a limit; the memory and the
speed are).

## 2. The classes

### A. Code-drawn 2D (the early games and the small toys)

Everything is drawn with the primitives of `gfx.h` in each frame: rectangles, circles, lines, gradients, soft edges,
glows, the stroke font and the Bungee faces (`mfont.h`). A pseudo-3D effect is also code: the road rows of DRIVE,
the horizon of CROAK and PIER, the tiles in space of STAR ROAD.

- Games: PONG, BUBBLE, BURGER, HOP, KABOOM, PRISM, STACK, TILT, TILES, SAFE, WHACK, EGG RUN, ZAP, RALLY,
  ECHO, SPLAT, RUN (its art is made by code at the start: `apps/run/run_stub_art.inc`), KNIGHT, SHAKE, CROAK, PINBALL,
  WOBBLE, MERGE, PUCK, JUNCTION, GRAINS, ROPE, TENNIS, SIMON, DRUMS, PADS,
  DIVE, PIER, POUR, DRIVE, STAR ROAD, MINESWEEPER, READ THE SNOW (its case files are made on the PC), DEEP CURRENT
  (the glowing creature), MURANO (the glass model of the concept page, ported to C), ORBITAL (the flat ships of the look
  page, a planet texture made at the open, the orbits), GLASSHOUSE (the painters of the
  look page in C draw a diorama once into layers at 2 texels a px; the parts of the physics are pictures that go out of
  the layer while they move: `apps/glasshouse/gl_view.inc`), SUMMER CUP (summer events with real physics, round
  first: the far background is one pass of rows with a fine dither, the near layer of each event a second pass, the
  rider of soft capsules and a Verlet ragdoll; whole spans with 32-bit writes: `apps/summer/sc_gfx.inc`), COIL RIDER
  (polygon terrain with holes: one pass of rows with a list of the active edges and the even-odd rule of each shape; a
  texture tile in world units for the solid, a counter for each column gives the bright edge at the top of each solid
  run: `apps/coil/cr_draw.inc`; a PC tool with shapely, not in this repo, makes the levels).
  JELLY COURIER (a soft moped that tows a trailer of jelly: the drawing core of SUMMER CUP with a camera roll as a
  shear of the whole picture, a town of hashed pastel houses in the far pass of rows, glossy jelly as layered ellipses
  with squash and stretch: `apps/jelly/jl_draw.inc`, `jl_rig.inc`).
  TRIVIA (a quiz: cards with a border band and a gloss, Fluent Emoji icons and a logo made on the PC, the Hebrew text
  drawn by the app; the background leaves out the boxes that a solid card covers, so each px of the frame is written
  once: `apps/trivia/tv_draw.inc`).
  RIM RUNNER (neon rings on deep black: each ring of the wheel is a table of colours by the distance to the ring,
  made once into a picture of row spans and copied in each frame; the things are drawn over it with dirty tiles of
  16 x 8 px: `apps/rim_runner/rr_view.inc`).
- Strong points: no data, any layout per board kind, fast to change, small app files.
- Weak points: the look stays "drawn". Flat code art is not enough for the big games (see section 4). Good for
  arcade toys, instruments, board games and abstract looks (neon, glow).
- Tools: none. Pictures for the QA: a PC screenshot tool (not in this repo).

### B. Cartoon puppets (`libs/toon`)

A small rasterizer with cartoon puppets (big heads, noodle arms, squash and stretch), a stage and a VHS filter.

- Games: FIGHT (8 fighters, the VHS look), the people of WHITE CITY.
- Tools: none (the puppets are code). The camera check: a PC tool in C (not in this repo).

### C. Code-drawn quest scenes (the "paper cut" family)

The places of the quest are lists of shapes in C (`*_art.inc`): polygons, gradients, ink lines, rain, light. The still
layer of a place is drawn once into a strip cache; each frame draws the people, the talk close-ups in the place and
the UI over it. The family: KEEPER first, then ROOM (a Python generator, not in this repo, MAKES `app_room.c` from
KEEPER and parts), FRAME, STAND-IN, SIDE B, LAST SHOW (each a copy, kept by hand, with its own look: layers, clay,
ink, riso dots).

- The people of some quests come from Python art tools (not in this repo).
- Put the talk close-ups in the place (as in SIDE B), and give each quest its own look.
- Strong points: no data, sharp at any zoom. Weak points: much hand work for each place; the speed falls with a big
  zoom (the still layer is drawn live in a close-up).

### D. Offline art generators (pixel art and canvas pictures)

Python or JavaScript generators draw the art at its real pixel size on the PC; an export step writes C arrays (often
zlib) into the app. The board blits sprites, tiles and palettes.

- CRUISE and SUNSET: Python art tools (the approved pipeline: `sprites.py`, `export_all.py`; not in this repo), the
  HD path for the close cast of SUNSET.
- SCENES: 14 hand pixel-art scenes as JS pages, baked by a `bake.js` script (private art tools). The rules for this
  look: full resolution, an own angle for each scene, dense detail; no low effort, and not too real.
- WRONG ROOM: the hero rig from a JS generator, the levels from Python scripts (private art tools).
- GUMMY SIEGE: JS canvas drawings (`apps/gummy_siege/tools/art/bake.js`) that headless Edge renders; `bake.py` packs
  them into `art.bin` (RGB565, alpha, palette pictures for the far layers).
- DEAD PIXEL, STATION X, RADIO NEPTUNE (pixel-art quests), JUNGLE RUN (the Atari 2600 look, by code with sprite
  tables).
- PIXEL STAGE (a test): a LIVING pixel-art picture. One painted picture is split on the PC into depth
  layers with an effect code for each px (flame, smoke, cloud, tree, glow) and the fire light; the board animates the
  effect px at 15 Hz, flickers the light through the colour table and draws the screen with a parallax and a free zoom
  (2 - 5x) at 32 - 46 fps on the 3.49 (a private art tool; MANUAL.md "PIXEL STAGE").
- Strong points: a sharp, chosen look at 1:1 pixels; with PIXEL STAGE also a free zoom and a camera. Weak points: each
  new pose or place is new art.

### E. Pre-rendered pictures, 2D play

A 3D scene (three.js or Blender) renders the backgrounds or the pieces once on the PC. The app keeps them as JPEG or
zlib pictures and plays in 2D over them.

- WHITE CITY (QUEST): three.js renders of the house in 3 times, as JPEG (a private export tool), decoded by the ROM
  tjpgd. This look is much better than a code-drawn look of the same house.
- STORM WATCH: the village as a Blender render at 3 zoom levels (JPEG, decoded to PSRAM at the start; a private
  Blender scene, `apps/village/tools/mk_data.py`).
- Board games: CHESS (Blender pieces and woods: a private Blender script), CHECKERS and BACKGAMMON (Blender pieces:
  private Blender scripts), SEA BATTLE; each with `apps/<app>/tools/mk_art.py`.
  ECHO KEYLLER (two Blender pictures), BOULEVARD (Blender art, `apps/boulevard/tools/mk_art.py`).
- Weak point: a picture is one camera; a zoom needs more renders (STORM WATCH: 3 levels).

### F. 2.5D: Blender sprites and textures on a world that the code draws

Blender renders the sprites (with alpha, at many sizes or headings), the textures and the far layers; the board
draws the world in code (a pseudo-3D road, a turned ground, parallax) and scales the sprites into it.

- RIVIERA GP: the cars and the props at 9 sizes, a 360 degree panorama, the road textures mapped on each row
  (private Blender scripts `race3d.py`, `race_assets.py`; `apps/riviera/tools/mk_data.py`).
- DIRT KINGS: one Blender picture of each arena from above, turned and zoomed in tiles of 8 x 8 px each frame; the
  trucks at 64 headings (`apps/dirt_kings/tools/dk_arena.py`, `mk_data.py`); `app_dirt25.c` is the 2.5D view.
- ZIKI RUSH: Blender parallax layers, the track texture, the sprites (`apps/ziki_rush/tools/zk_art.py`, `mk_data.py`).
- Ray casters: DEAD LINE (the textures come from the approved look page, through a private export script) and TEAM
  BUILDING (a pixel-art 3D paintball shooter: `paint_ray.inc`, the figures with `rig.h`, the maps from a private
  Python tool).
- BEEPER 97 and GOLDEN HOUR: a ground picture baked on the PC (`apps/beeper/tools/gen_city.js`, on the SD card) with
  lit 3D polygons for the buildings, the cars and the people.
- Strong points: a rich look at a high speed. Weak points: big app files or packs; many sprite sizes or headings.

### G. Real-time 3D polygons

The board transforms, lights and draws triangles in each frame.

- IRON ARENA: an own engine (bands of rows on two cores; the stage, the crowd, the fighters).
- BEEPER 97: the city blocks, the cars and the people as lit polygons on the baked ground.
- THE CUBBY ENGINE (its 3D core is `libs/tf3d/tf3d.inc`: AFTER HOURS 3D, NIGHT TAPES, THE MODEL, INK KARTS and CUBBY include it): a tile renderer (64 x 16 tiles, a display
  list on core 0, the tiles on both cores, a z-buffer, round meshes by code, smooth colour, soft edges, glows, a HUD
  of cached masks). It is the base of class I. Its look is smoother than the look of IRON ARENA. THE MODEL (an open 3D
  world with it) did not make a fun game, and its look is less good than the look of CUBBY.
- INK KARTS (`apps/kart`): a kart race on the CUBBY core (`libs/tf3d`, the settings in `kt_r3d.inc`; `kt_mesh.inc`,
  `kt_hud.inc`). The tracks, the karts and the props are code (no data). The ground and the skyline are the per-pixel
  background rows (no triangles); the ink ribbons are a glossy paint blend (DK_ADD is a mix toward the colour).
  The round frame (APP_ROUND_HI: 466 x 466 on the 1.75C, its size at run time) and the 3.49 draw the 3D at half the
  width and half the height (KY = 2, a soft vertical blend in the copy out); the round frame skips the tiles out of the
  circle. The 3.49: 22 - 31 fps in a race.
- MARSH KATA (a test): 3D AS PIXEL ART. The world at half the frame size, each px a step of one ramp with
  an ordered dither (the palette of each move is a new ramp for the whole scene), a floor pass in place of ground
  triangles; the fighter at the full resolution with an outline of one px, laid over the world by depth
  (`apps/marsh`, MANUAL.md "MARSH KATA").
  THE RIVAL PACHA is TRUE PIXEL ART from 3D on the same pixel grid: flat materials (`mat_t.flat`: 6
  bands + a line colour of a hand-picked palette, a checker only at band edges, no dither), a sprite light, a clean-up
  of lone px, its own outline colour, eyes stamped px by px, px that fall out at a hit (`mk_rival.inc`).
- The cost of a triangle is its setup (about 800 cycles) and its rows (60 - 100 cycles), not its pixels: count the
  triangles. The 3.49: CUBBY 25 - 32 fps in play.

### H. Painted places in an SD data pack (the "Lost City" adventures)

An offline painter on the PC (numpy) paints each place of the adventure; the board shows the picture and draws the
layers (the things that change), the gentle 2D animation (`*_anim.inc`: screens, steam, motes, water) and the UI.
There is no 3D on the board.

- FOUR SEASONS: scenes in Python (`apps/four_seasons/tools/sc_*.py`, `bake.py`), a palette and 1 byte a pixel for each
  season, layers for the things that change, the blend of two seasons (`/sdcard/seasons/`).
- THE TURNING WOOD: a private numpy painter (foliage as a density field, trunks, ruins) with the soft finish
  `soft.py` (a guided filter, bloom, a film curve, 256 colours and a fine dither); TWP pictures (a palette); sub-pixel
  animation (`tw_anim.inc`).
- AFTER HOURS and STUCK AT 99%: a private numpy 3D room renderer (`r3.py`: boxes, a z-buffer, point lights, a sun
  with soft edges, glass, a glossy floor) with a stronger soft finish; AHP1 pictures (640 x 240 RGB565 + zlib, 65536
  colours), AHL1 layers and sprites. Use more colours and softer pictures.
- Formats: TWP (palette), AHP1 (RGB565 + zlib), AHL1 (a layer: 0 = clear). The data packs are 12 - 17 MB.
- The Tab5 (640 x 360): a second pack `<id>_t5` with the same scenes in a taller view (the painters with
  `AH_H=360` / `TW_H=360`: the same camera, 60 more rows above and below; the 240-row picture = the middle rows, so
  the touch tables of the game stay). The store gives `<id>_t5` only to a Tab5 board.
- Weak points: no camera motion, and the hero is not in the picture (a 2D sprite at most).

### I. A Blender stage with live 3D actors (the newest class)

It is the class for the point-and-move quests. Each place is a Blender scene; each shot renders on the PC, and the
board draws only what moves, in 3D, with a z-buffer that starts from the depth of the picture. So the hero walks
behind a desk with no masks.

- Games: AFTER HOURS 3D (`apps/ah3d`), NIGHT TAPES (`apps/night_tapes`). The Blender and bake tools below are in
  private work folders (not in this repo); the text names the method.
- The render (`blender/render.py`, Blender 4.3 Cycles, OptiX): the colour at 2x (256 samples), the planar depth and
  the tap map (the object index) at 1x, a border of 32 px at the sides and 16 px at the top and the bottom for the
  tilt view (a close-up has none); `--check` makes quick pictures with the actors for the work on a place.
- The places: `blender/places/<pid>.py` with the kit `blender/kit.py` (boxes, materials, lights, `thing`, `exit`,
  `actor`, `part`, `anim`, `shot`, `closeup`; NIGHT TAPES adds `blender/tkit.py`: `cam_shot`, `walk_shot`, `mark`,
  `tape_actor`). Agents can build places from a brief (`BRIEF.md`).
- The bake (`bake.py`): one A3S1 file for each shot and mode (the camera, the light of the actors from the picture,
  the colour RGB565 with the depth in one 32-bit word, the tap map; zlib) and the C tables (`*_scene.c`: the places,
  the shots, the things, the ways out, the actors, the parts, the walk grid of 0.2 m cells, the meshes). NIGHT TAPES
  also bakes the VIEW MAP (the walk shots that show a person on each cell, and the best one), the people and the props
  of the night, and their timelines; and a colour grade of the morning pictures (`grade.py`: vibrance, a soft S-curve,
  a soft top, a warm light, never brighter).
- The engine: the CUBBY tile renderer (class G) with the picture as the start of each tile. Each pixel of the picture
  keeps its depth (`ZK / z`); the actors (the hero, the doors, the leaves of the lifts, the moving parts of the
  puzzles, the people of the tapes) draw only where they are nearer. The gentle animation goes into the tiles before
  the actors. A camera filter (the tapes) also runs in the tiles, on both cores.
- The checks before the bake (NIGHT TAPES): `spot_check.py` (each thing and way out in a walk view; the walk floor
  joins all spots), `cover.py --search` (the floor that the walk views show, and a search for cameras with no render),
  `taps_check.py` (a tap in each view can bring each other view), `prove_night.py` (each clip person in the camera
  view for the whole window; `design/prove_night.py`). The PC bot (`test/play.c`) plays the whole game with real taps, also with ASan and
  UBSan; `board_*.py` plays the same taps on a board and compares.
- A bigger frame (the Tab5, 640 x 360): the same cameras render again with a taller view
  (`render.py <PID> --t5`; a close-up gets a longer lens so that its thing keeps its size), `bake.py --t5` makes a
  pack of its own and `*_scene_t5.inc` (only the tables that depend on the picture; the other tables stay one set
  for all boards), and the app reads that pack on that board kind (`VH` 360). The store keeps it under the id
  `<app id>_t5`. MANUAL.md "AFTER HOURS" (the Tab5 part) has the details.
- Strong points: the best look (real light, soft shadows) with a live hero and real occlusion; the camera can pan and
  tilt a little. Weak points: each camera is a render (fixed views); big packs; the PSRAM and the internal RAM are
  tight.

## 3. The costs on the board (the lessons)

- PSRAM is slow: about 40 MB/s for the frame; a copy of a 640 x 172 screen takes 11 - 13 ms. Do per-pixel work in the
  tile buffers in the internal RAM (both cores), not in a pass over the whole frame in PSRAM (the camera filter of
  NIGHT TAPES: 5 - 9 fps in one frame pass, 8 - 21 fps in the tiles).
- Clip each loop to the tile. A loop over the whole box of a glow disc in each tile (a NIGHT TAPES fault) takes 5.8 s
  a frame for a glow near a camera, and the task watchdog warns. AFTER HOURS 3D has the same latent loop (`an_disc`).
- A tap circle of a big 3D actor near the camera covers the screen: limit it, and let a thing of the picture in front
  of the actor win.
- Give the mesh store room for all actors (NIGHT TAPES: the 5 people of the night filled 11000 triangles, and the
  doors of the places had no mesh). Make the PC test fail on a lost mesh.
- In an app file `sinf` / `cosf` take about 12 us a call: use `fsin` / `fcos` of `gfx.h` (a table).
- The internal RAM in a game is about 17 KB free on the 3.49 with the WiFi on: the tile buffers need 16 KB free to stay
  there. The core-1 worker must rest a little (a few ms each 20 frames) or the task watchdog warns.
- The SD card: big reads into PSRAM need a DMA buffer in the internal RAM; the firmware keeps one (`main/sd_dma.h`).
  The app tries again on a fault and loads a place in the black of a cut (0.2 - 1 s).

## 4. The rules for a new look

- A nice look is not enough: a game needs a fresh core rule (LAMPLIGHTER stopped at the concept: a too common idea).
- Smooth and rich: round shapes, smooth colour, soft edges, smooth motion (smoother than IRON ARENA). Use more
  colours and softer pictures. Not synthetic, not polygon-like, and not washed out (appealing colours, but the
  picture must not look too bright).
- The things that the player can use stand out (radiant), the rest is calmer (THE MODEL).
- A reference picture means the game screen must look like it: a close camera and a dense scene. Make a mock-up at
  the real screen size first (WALKING HOUSES failed: tiny houses on an empty grid).
- A cinematic camera for action games: zooms, close-ups, shake, a roll; the player stays in the frame.
- Each board kind looks good in its natural position; check real board pictures (`/api/screen`, the console `shot`),
  not only PC pictures. A board with no SD card shows NO DATA for classes H and I.
- Real fonts for big text (an anti-aliased atlas), never the stroke font for big numbers.
- Gentle animation in each still picture (screens, steam, motes, water).

## 5. Which class for a new game

| The game | The class to start from |
| --- | --- |
| An arcade toy, a board game, an instrument, a neon or abstract look | A (and E for rich pieces) |
| A point-and-click or point-and-move quest | I (start from `apps/ah3d` and its tools) |
| A puzzle adventure with no hero on the screen | H (start from THE TURNING WOOD: all its tools are in git) |
| A racer, a platformer, a top-down game with rich art | F (Blender sprites and textures) |
| A 3D action game with a free camera | G (the 3D core of `apps/ah3d` or the CUBBY engine; mind the triangle count) |
| A retro or pixel game | D (the CRUISE pipeline) |

For each new look: a look test first (a page or real-size pictures for the reviewer), then the full game. Keep the art
tools that rebuild the data in `apps/<app>/tools/`, and write the class and the tools in the `MANUAL.md` section of
the game.
