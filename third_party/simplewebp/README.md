# simplewebp

A WebP decoder in one file (lossy and lossless), a port of libwebp: `simplewebp.h` of
https://github.com/MikuAuahDark/simplewebp (version 20260718), with one change (a "TFN OS" comment): a read that goes
past the end of a chunk (`swebp__proxy_read`) gives only the bytes up to that end. Before, it gave more bytes than the
caller asked for: a damaged file wrote past the buffer of the caller. BSD-3-Clause and the WebM patent grant
(`LICENSE`; NOTICE.md). WIKI uses it on the Tab5 for the pictures of the articles (`apps/wiki/wk_webp.c`).
