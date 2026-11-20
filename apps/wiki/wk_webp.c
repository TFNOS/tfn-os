// WIKI: the WebP decoder for the pictures of the articles (only the Tab5; libs/simplewebp, BSD-3-Clause:
// NOTICE.md). The other boards show the text only: this file is empty there.
// The task: the app task of TFN OS calls it (through wk_pics.inc).
// The memory: the caller owns the pixels of each picture (wk_pics.inc).
#include "tfn/board.h"  // (BOARD_UI_TABLET)
#if BOARD_UI_TABLET
#define NDEBUG  // (no assert() in the app file)
#define SIMPLEWEBP_IMPLEMENTATION
#define SIMPLEWEBP_DISABLE_STDIO
#include "simplewebp/simplewebp.h"
#endif
typedef int wk_webp_t;  // (a C file must have one declaration)
