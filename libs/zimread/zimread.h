// ZIMREAD: a reader of ZIM files (the offline Wikipedia of Kiwix and openZIM) on the SD card, for the WIKI and the
// DICTIONARY apps. Pure C on stdio (fopen, fread, fseek): it runs on the board and on the PC (the tests). A module of
// the firmware: its functions are ABI names (an app includes this header and calls the firmware).
//
// - The file: one .zim file, or split parts (name.zimaa, name.zimab, ... at most 32; the parts of Kiwix are 2 GB, the
//   copy tool of WIKI makes 128 MB parts: a fault of the WiFi copies one part again, not the whole file). A single
//   file above 2 GB is read with a seek in two steps (the offset of fseek on the board is a signed 32-bit number).
// - The entries: the URL pointer list (all entries, sorted by namespace and URL) and the title listing
//   "X/listing/titleOrdered/v1" (the entries of the articles, sorted by title: the search by prefix).
// - The content: a cluster holds many blobs (zstd, or no compression). A zstd cluster is decoded into one buffer in
//   PSRAM that is also the window of the decoder (the frames of libzim have an 8 MB window and no content size), and the
//   decoder stops as soon as the blob is complete. The last decoded cluster stays (the next blob of it is free).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ZIM_URL_MAX   240
#define ZIM_TITLE_MAX 240
#define ZIM_REDIRECT  0xFFFFu  // the MIME number of a redirect entry
#define ZIM_NONE      0xFFFFFFFFu

typedef struct zim_s zim_t;

typedef struct {
    uint16_t mime;           // an index into the MIME list, or ZIM_REDIRECT (0xFFFE, 0xFFFD: old kinds)
    char ns;                 // the namespace: 'C' (the content of new files), 'A', 'M', 'X', 'W' ...
    uint32_t cluster, blob;  // the place of the content (a content entry)
    uint32_t redirect;       // the target entry (a redirect)
    char url[ZIM_URL_MAX];
    char title[ZIM_TITLE_MAX];  // the URL when the entry has no own title
} zim_entry_t;

// Open a ZIM file (path = name.zim; the parts name.zimaa ... are found when name.zim is not there). err gets a short
// English reason when the result is NULL.
zim_t *zim_open(const char *path, char *err, int errlen);
void zim_close(zim_t *z);

uint32_t zim_entry_count(const zim_t *z);
uint32_t zim_main_page(const zim_t *z);               // ZIM_NONE: no main page
const char *zim_mime(const zim_t *z, uint16_t mime);  // "" for a number that is not in the list
uint64_t zim_size(const zim_t *z);
int zim_parts(const zim_t *z);

// One entry by its place in the URL list. false: a read fault.
bool zim_entry(zim_t *z, uint32_t idx, zim_entry_t *e);
// The content entry at the end of the redirects (at most 8 steps). ZIM_NONE: a loop or a fault.
uint32_t zim_follow(zim_t *z, uint32_t idx);
// The entry of a namespace + URL (a link of an article: "C" + "Tel_Aviv"). ZIM_NONE: not found.
uint32_t zim_find_url(zim_t *z, char ns, const char *url);

// THE TITLES: the count of the title listing (0: the file has none), the entry at place i of it, and the first place
// whose title is not less than prefix (a byte compare of UTF-8, as the listing is sorted).
uint32_t zim_title_count(zim_t *z);
uint32_t zim_title_entry(zim_t *z, uint32_t i);
uint32_t zim_title_lower_bound(zim_t *z, const char *prefix);

// The content of a content entry: a pointer that stays valid until the next zim_blob() or zim_close(), and its length.
// NULL: a read fault, no memory, or a compression that this reader does not know (xz).
const uint8_t *zim_blob(zim_t *z, uint32_t cluster, uint32_t blob, uint32_t *len);
// Free the decoded cluster (the app leaves, or needs the PSRAM).
void zim_drop_cache(zim_t *z);

// (for the tests and the log) the bytes read from the card, the clusters decoded, the time of the last decode in ms,
// the opens of a part file (one part is open at a time)
typedef struct {
    uint64_t bytes_read;
    uint32_t decodes, seeks;
    uint32_t last_decode_ms;
    uint32_t part_opens;
} zim_stats_t;
void zim_stats(const zim_t *z, zim_stats_t *s);
