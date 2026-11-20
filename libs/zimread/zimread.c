// ZIMREAD: see zimread.h. The format: https://wiki.openzim.org/wiki/ZIM_file_format (version 5 and 6).
#include "zimread.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define ZSTD_STATIC_LINKING_ONLY  // (ZSTD_d_stableOutBuffer: the output buffer is the window)
#include "zstd/zstd.h"

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "esp_timer.h"
static void *big_alloc(size_t n)
{
    return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}
#else
#include <time.h>
static void *big_alloc(size_t n)
{
    return malloc(n);
}
static uint32_t now_ms(void)
{
    return (uint32_t)(clock() * 1000 / CLOCKS_PER_SEC);
}
#endif

#define MAX_PARTS       32    // (name.zimaa .. name.zimbf: parts of 128 MB for a 4 GB file)
#define BLOCK           4096  // the read cache: blocks of the file (entries, pointers, offsets)
#define NBLOCK          16
#define IN_CHUNK        32768            // the input of the zstd decoder: one read of the card
#define CLUSTER_CAP0    (2304u * 1024u)  // the first size of the cluster buffer (the clusters of libzim: about 2 MB)
#define CLUSTER_CAP_MAX (8u * 1024u * 1024u)

typedef struct {
    uint64_t pos;  // the place in the file (UINT64_MAX: empty)
    uint32_t len, age;
    uint8_t *data;
} block_t;

struct zim_s {
    // ONE part is open at a time (cur_part; -1: none): the SD card mount of the board allows only 4 open files
    FILE *f;
    char path[260];
    bool split;
    uint64_t part_start[MAX_PARTS + 1];
    int nparts, cur_part;
    uint64_t size;
    uint32_t entries, clusters, main_page;
    uint64_t url_ptr, cluster_ptr, checksum_pos;
    char *mimes;  // the MIME list: strings one after the other
    int nmime;
    uint16_t mime_off[64];
    block_t blk[NBLOCK];
    uint32_t tick;
    // the title listing (found at the first use): in memory (a compressed cluster), or its place in the file (a cluster
    // with no compression: the new files of Kiwix; the English top file has 703,608 titles = 2.8 MB)
    uint32_t *titles;
    uint64_t titles_at;
    uint32_t ntitles;
    bool titles_tried;
    // the decoded cluster (a decode that can go on: the same buffer, the same decoder, the next input place)
    uint32_t c_num;
    uint8_t *c_buf;
    size_t c_cap, c_pos;
    bool c_done, c_ext;
    uint64_t c_in;  // the next byte of the compressed input in the file
    ZSTD_DCtx *dctx;
    uint8_t *in;  // the input chunk
    size_t in_len, in_pos;
    uint8_t *raw;  // a blob of a cluster with no compression
    zim_stats_t st;
};

// ---------------------------------------------------------------- the file and its parts

static bool seek_part(FILE *f, uint64_t pos)
{
#ifdef _WIN32
    return _fseeki64(f, (long long)pos, SEEK_SET) == 0;
#else
    // the offset of fseek is a signed 32-bit number on the board: above 2 GB, two steps (the FAT layer adds them as
    // an unsigned 32-bit place)
    if (pos <= 0x7FFFFFFFull) {
        return fseek(f, (long)pos, SEEK_SET) == 0;
    }
    if (fseek(f, 0x7FFFFFFFL, SEEK_SET) != 0) {
        return false;
    }
    return fseek(f, (long)(pos - 0x7FFFFFFFull), SEEK_CUR) == 0;
#endif
}

static void part_name(char *name, size_t len, const char *base, int i)
{
    snprintf(name, len, "%s%c%c", base, i < 26 ? 'a' : 'b', 'a' + i % 26);  // name.zimaa .. zimaz, zimba ..
}

#ifdef ZIM_MAX_OPEN
int zim_open_files, zim_open_files_max;  // (the PC test: the count of the open part files)
#define OPENED()                                                                                                       \
    do {                                                                                                               \
        if (++zim_open_files > zim_open_files_max)                                                                     \
            zim_open_files_max = zim_open_files;                                                                       \
    } while (0)
#define CLOSED() (zim_open_files--)
#else
#define OPENED() ((void)0)
#define CLOSED() ((void)0)
#endif

// the part p open in z->f (the part before it closes first)
static FILE *use_part(zim_t *z, int p)
{
    if (z->cur_part == p && z->f) {
        return z->f;
    }
    if (z->f) {
        fclose(z->f);
        CLOSED();
    }
    z->f = NULL;
    z->cur_part = -1;
    if (z->split) {
        char name[300];
        part_name(name, sizeof name, z->path, p);
        z->f = fopen(name, "rb");
    } else {
        z->f = fopen(z->path, "rb");
    }
    if (!z->f) {
        return NULL;
    }
    OPENED();
    z->cur_part = p;
    z->st.part_opens++;
    return z->f;
}

// read n bytes at pos (across the parts); the count that was read
static size_t raw_read(zim_t *z, uint64_t pos, void *dst, size_t n)
{
    size_t got = 0;
    while (got < n) {
        int p = 0;
        while (p < z->nparts - 1 && pos >= z->part_start[p + 1]) {
            p++;
        }
        uint64_t in_part = pos - z->part_start[p];
        uint64_t left = z->part_start[p + 1] - pos;
        size_t want = n - got;
        if (want > left) {
            want = (size_t)left;
        }
        if (want == 0) {
            break;
        }
        FILE *f = use_part(z, p);
        if (!f || !seek_part(f, in_part)) {
            break;
        }
        z->st.seeks++;
        size_t r = fread((uint8_t *)dst + got, 1, want, f);
        z->st.bytes_read += r;
        got += r;
        pos += r;
        if (r < want) {
            break;
        }
    }
    return got;
}

// small reads through the block cache
static bool cread(zim_t *z, uint64_t pos, void *dst, size_t n)
{
    uint8_t *d = dst;
    while (n) {
        uint64_t bpos = pos / BLOCK * BLOCK;
        block_t *b = NULL, *old = &z->blk[0];
        for (int i = 0; i < NBLOCK; i++) {
            if (z->blk[i].pos == bpos && z->blk[i].data) {
                b = &z->blk[i];
                break;
            }
            if (z->blk[i].age < old->age) {
                old = &z->blk[i];
            }
        }
        if (!b) {
            b = old;
            if (!b->data && !(b->data = big_alloc(BLOCK))) {
                return false;
            }
            b->pos = UINT64_MAX;
            b->len = (uint32_t)raw_read(z, bpos, b->data, BLOCK);
            b->pos = bpos;
        }
        b->age = ++z->tick;
        uint32_t off = (uint32_t)(pos - bpos);
        if (off >= b->len) {
            return false;
        }
        size_t take = b->len - off;
        if (take > n) {
            take = n;
        }
        memcpy(d, b->data + off, take);
        d += take;
        pos += take;
        n -= take;
    }
    return true;
}

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}
static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint64_t rd64(const uint8_t *p)
{
    return (uint64_t)rd32(p) | (uint64_t)rd32(p + 4) << 32;
}

zim_t *zim_open(const char *path, char *err, int errlen)
{
#define FAIL(msg)                                                                                                      \
    do {                                                                                                               \
        if (err)                                                                                                       \
            snprintf(err, (size_t)errlen, "%s", msg);                                                                  \
        zim_close(z);                                                                                                  \
        return NULL;                                                                                                   \
    } while (0)
    zim_t *z = calloc(1, sizeof *z);
    if (!z) {
        if (err) {
            snprintf(err, (size_t)errlen, "no memory");
        }
        return NULL;
    }
    z->c_num = ZIM_NONE;
    z->cur_part = -1;
    for (int i = 0; i < NBLOCK; i++) {
        z->blk[i].pos = UINT64_MAX;
    }
    if (strlen(path) >= sizeof z->path) {
        FAIL("the name is too long");
    }
    strcpy(z->path, path);
    if (use_part(z, 0)) {
        // one whole file: its size from the end (FAT: under 4 GB; ftell as an unsigned 32-bit number on the board)
        uint64_t len = 0;
#ifdef _WIN32
        _fseeki64(z->f, 0, SEEK_END);
        len = (uint64_t)_ftelli64(z->f);
#else
        fseek(z->f, 0, SEEK_END);
        len = (uint64_t)(uint32_t)ftell(z->f);
#endif
        z->nparts = 1;
        z->part_start[1] = len;
    } else {
        // the parts: their sizes from stat(), no open file (a part is under 2 GB)
        z->split = true;
        for (int i = 0; i < MAX_PARTS; i++) {
            char name[300];
            struct stat sb;
            part_name(name, sizeof name, path, i);
            if (stat(name, &sb) != 0) {
                break;
            }
            z->part_start[i + 1] = z->part_start[i] + (uint64_t)(uint32_t)sb.st_size;
            z->nparts++;
        }
        if (!z->nparts) {
            FAIL("no file");
        }
    }
    z->size = z->part_start[z->nparts];
    uint8_t h[80];
    if (raw_read(z, 0, h, 80) != 80) {
        FAIL("cannot read the header");
    }
    if (rd32(h) != 0x044D495Au) {
        FAIL("not a ZIM file");
    }
    if (rd16(h + 4) < 5) {
        FAIL("an old ZIM version");
    }
    z->entries = rd32(h + 24);
    z->clusters = rd32(h + 28);
    z->url_ptr = rd64(h + 32);
    z->cluster_ptr = rd64(h + 48);
    uint64_t mime_pos = rd64(h + 56);
    z->main_page = rd32(h + 64);
    z->checksum_pos = rd64(h + 72);
    if (z->url_ptr + 8ull * z->entries > z->size || z->cluster_ptr + 8ull * z->clusters > z->size) {
        FAIL("the file is cut (or a seek above 2 GB failed)");
    }
    // the MIME list (at most 64 types, 2 KB)
    z->mimes = calloc(1, 2049);
    if (!z->mimes || !cread(z, mime_pos, z->mimes, 2048)) {
        FAIL("cannot read the MIME list");
    }
    for (int off = 0; off < 2048 && z->nmime < 64 && z->mimes[off];) {
        z->mime_off[z->nmime++] = (uint16_t)off;
        off += (int)strlen(z->mimes + off) + 1;
    }
    return z;
#undef FAIL
}

void zim_close(zim_t *z)
{
    if (!z) {
        return;
    }
    if (z->f) {
        fclose(z->f);
        CLOSED();
    }
    for (int i = 0; i < NBLOCK; i++) {
        free(z->blk[i].data);
    }
    zim_drop_cache(z);
    free(z->titles);
    free(z->mimes);
    free(z);
}

uint32_t zim_entry_count(const zim_t *z)
{
    return z->entries;
}
uint32_t zim_main_page(const zim_t *z)
{
    return z->main_page;
}
uint64_t zim_size(const zim_t *z)
{
    return z->size;
}
int zim_parts(const zim_t *z)
{
    return z->nparts;
}
const char *zim_mime(const zim_t *z, uint16_t m)
{
    return m < z->nmime ? z->mimes + z->mime_off[m] : "";
}
void zim_stats(const zim_t *z, zim_stats_t *s)
{
    *s = z->st;
}

// ---------------------------------------------------------------- the entries

bool zim_entry(zim_t *z, uint32_t idx, zim_entry_t *e)
{
    uint8_t p[8];
    if (idx >= z->entries || !cread(z, z->url_ptr + 8ull * idx, p, 8)) {
        return false;
    }
    uint64_t at = rd64(p);
    uint8_t d[16 + ZIM_URL_MAX + ZIM_TITLE_MAX];
    // (the entry can be at the end of the file: read what is there)
    size_t n = sizeof d;
    if (at + n > z->size) {
        n = (size_t)(z->size - at);
    }
    if (n < 12 || !cread(z, at, d, n)) {
        return false;
    }
    memset(e, 0, sizeof *e);
    e->mime = rd16(d);
    e->ns = (char)d[3];
    size_t k;
    if (e->mime == ZIM_REDIRECT) {
        e->redirect = rd32(d + 8);
        k = 12;
    } else if (e->mime >= 0xFFFD) {
        k = 8;  // (an old link target or a deleted entry)
    } else {
        e->cluster = rd32(d + 8);
        e->blob = rd32(d + 12);
        k = 16;
    }
    size_t i = 0;
    while (k < n && d[k] && i < ZIM_URL_MAX - 1) {
        e->url[i++] = (char)d[k++];
    }
    while (k < n && d[k]) {
        k++;  // (a URL that is too long: cut)
    }
    k++;
    i = 0;
    while (k < n && d[k] && i < ZIM_TITLE_MAX - 1) {
        e->title[i++] = (char)d[k++];
    }
    if (!e->title[0]) {
        memcpy(e->title, e->url, sizeof e->title);
    }
    return true;
}

uint32_t zim_follow(zim_t *z, uint32_t idx)
{
    zim_entry_t e;
    for (int step = 0; step < 8; step++) {
        if (!zim_entry(z, idx, &e)) {
            return ZIM_NONE;
        }
        if (e.mime != ZIM_REDIRECT) {
            return idx;
        }
        idx = e.redirect;
    }
    return ZIM_NONE;
}

uint32_t zim_find_url(zim_t *z, char ns, const char *url)
{
    uint32_t lo = 0, hi = z->entries;
    zim_entry_t e;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (!zim_entry(z, mid, &e)) {
            return ZIM_NONE;
        }
        int c = (unsigned char)e.ns - (unsigned char)ns;
        if (!c) {
            c = strcmp(e.url, url);
        }
        if (!c) {
            return mid;
        }
        if (c < 0) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return ZIM_NONE;
}

// ---------------------------------------------------------------- the titles

static void load_titles(zim_t *z)
{
    if (z->titles_tried) {
        return;
    }
    z->titles_tried = true;
    uint32_t i = zim_find_url(z, 'X', "listing/titleOrdered/v1");
    if (i == ZIM_NONE) {
        i = zim_find_url(z, 'X', "listing/titleOrdered/v0");
    }
    zim_entry_t e;
    if (i == ZIM_NONE || !zim_entry(z, i, &e) || e.mime == ZIM_REDIRECT || e.cluster >= z->clusters) {
        return;
    }
    // a cluster with no compression: only the place of the blob (zim_title_entry reads 4 bytes through the cache)
    uint8_t p[16], info;
    if (cread(z, z->cluster_ptr + 8ull * e.cluster, p, 8) && cread(z, rd64(p), &info, 1) && (info & 0x0F) <= 1) {
        const uint64_t at = rd64(p);
        const int osz = (info & 0x10) ? 8 : 4;
        if (cread(z, at + 1 + (uint64_t)e.blob * (uint64_t)osz, p, (size_t)(2 * osz))) {
            const uint64_t o0 = osz == 8 ? rd64(p) : rd32(p), o1 = osz == 8 ? rd64(p + 8) : rd32(p + 4);
            if (o1 > o0 && (o1 - o0) / 4 < 0xFFFFFFF0u) {
                z->titles_at = at + 1 + o0;
                z->ntitles = (uint32_t)((o1 - o0) / 4);
            }
        }
        return;
    }
    uint32_t len = 0;
    const uint8_t *b = zim_blob(z, e.cluster, e.blob, &len);
    if (!b || len < 4) {
        return;
    }
    z->titles = big_alloc(len);
    if (!z->titles) {
        return;
    }
    memcpy(z->titles, b, len);  // (little-endian 32-bit entry numbers, as the board)
    z->ntitles = len / 4;
    zim_drop_cache(z);  // (the listing cluster is not an article)
}

uint32_t zim_title_count(zim_t *z)
{
    load_titles(z);
    return z->ntitles;
}

uint32_t zim_title_entry(zim_t *z, uint32_t i)
{
    load_titles(z);
    if (i >= z->ntitles) {
        return ZIM_NONE;
    }
    if (z->titles) {
        return z->titles[i];
    }
    uint8_t p[4];
    return cread(z, z->titles_at + 4ull * i, p, 4) ? rd32(p) : ZIM_NONE;
}

uint32_t zim_title_lower_bound(zim_t *z, const char *prefix)
{
    load_titles(z);
    uint32_t lo = 0, hi = z->ntitles;
    zim_entry_t e;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (!zim_entry(z, zim_title_entry(z, mid), &e)) {
            return hi;
        }
        if (strcmp(e.title, prefix) < 0) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo;
}

// ---------------------------------------------------------------- the content

void zim_drop_cache(zim_t *z)
{
    free(z->c_buf);
    z->c_buf = NULL;
    z->c_cap = z->c_pos = 0;
    z->c_num = ZIM_NONE;
    if (z->dctx) {
        ZSTD_freeDCtx(z->dctx);
    }
    z->dctx = NULL;
    free(z->in);
    z->in = NULL;
    free(z->raw);
    z->raw = NULL;
}

// a new decode of cluster c into a buffer of cap bytes (the place of its compressed data in the file: at)
static bool cluster_start(zim_t *z, uint32_t c, uint64_t at, uint8_t info, size_t cap)
{
    zim_drop_cache(z);
    z->c_buf = big_alloc(cap);
    z->in = big_alloc(IN_CHUNK);
    z->dctx = ZSTD_createDCtx();
    if (!z->c_buf || !z->in || !z->dctx) {
        zim_drop_cache(z);
        return false;
    }
    ZSTD_DCtx_setParameter(z->dctx, ZSTD_d_windowLogMax, 27);
    ZSTD_DCtx_setParameter(z->dctx, ZSTD_d_stableOutBuffer, 1);
    z->c_num = c;
    z->c_cap = cap;
    z->c_pos = 0;
    z->c_done = false;
    z->c_ext = (info & 0x10) != 0;
    z->c_in = at + 1;
    z->in_len = z->in_pos = 0;
    return true;
}

// decode until at least `need` bytes of the cluster are out. 1: done, 0: the buffer is too small, -1: a fault
static int cluster_more(zim_t *z, size_t need)
{
    uint32_t t0 = now_ms();
    ZSTD_outBuffer out = {z->c_buf, z->c_cap, z->c_pos};
    while (out.pos < need && !z->c_done) {
        if (out.pos == out.size) {
            return 0;
        }
        if (z->in_pos == z->in_len) {
            uint64_t left = z->size > z->c_in ? z->size - z->c_in : 0;
            size_t want = left < IN_CHUNK ? (size_t)left : IN_CHUNK;
            z->in_len = want ? raw_read(z, z->c_in, z->in, want) : 0;
            z->in_pos = 0;
            z->c_in += z->in_len;
            if (!z->in_len) {
                return -1;
            }
        }
        ZSTD_inBuffer in = {z->in, z->in_len, z->in_pos};
        size_t r = ZSTD_decompressStream(z->dctx, &out, &in);
        z->in_pos = in.pos;
        if (ZSTD_isError(r)) {
            return out.pos == out.size ? 0 : -1;
        }
        if (r == 0) {
            z->c_done = true;
        }
    }
    z->c_pos = out.pos;
    z->st.last_decode_ms = now_ms() - t0;
    return out.pos >= need ? 1 : -1;
}

const uint8_t *zim_blob(zim_t *z, uint32_t c, uint32_t b, uint32_t *len)
{
    if (c >= z->clusters) {
        return NULL;
    }
    uint8_t p[8];
    if (!cread(z, z->cluster_ptr + 8ull * c, p, 8)) {
        return NULL;
    }
    uint64_t at = rd64(p);
    uint8_t info;
    if (!cread(z, at, &info, 1)) {
        return NULL;
    }
    const int comp = info & 0x0F;
    const int osz = (info & 0x10) ? 8 : 4;
    if (comp == 0 || comp == 1) {
        // no compression: the two offsets of the blob, then the blob (it can be a big picture: read it whole)
        uint8_t o[16];
        if (!cread(z, at + 1 + (uint64_t)b * (uint64_t)osz, o, (size_t)(2 * osz))) {
            return NULL;
        }
        uint64_t o0 = osz == 8 ? rd64(o) : rd32(o), o1 = osz == 8 ? rd64(o + 8) : rd32(o + 4);
        if (o1 < o0 || o1 - o0 > 64u * 1024u * 1024u) {
            return NULL;
        }
        free(z->raw);
        z->raw = big_alloc((size_t)(o1 - o0) + 1);
        if (!z->raw) {
            return NULL;
        }
        if (raw_read(z, at + 1 + o0, z->raw, (size_t)(o1 - o0)) != (size_t)(o1 - o0)) {
            return NULL;
        }
        z->raw[o1 - o0] = 0;
        *len = (uint32_t)(o1 - o0);
        return z->raw;
    }
    if (comp != 5) {
        return NULL;  // (xz: not in the new files of Kiwix)
    }
    for (size_t cap = CLUSTER_CAP0; cap <= CLUSTER_CAP_MAX; cap *= 2) {
        if (z->c_num != c || z->c_cap < cap) {  // (a new cluster, or a bigger buffer: a new decode)
            if (!cluster_start(z, c, at, info, cap)) {
                return NULL;
            }
            z->st.decodes++;
        }
        // the offset table: the first offset tells the count of the offsets
        int r = cluster_more(z, (size_t)osz);
        if (r == 0) {
            continue;
        }
        if (r < 0) {
            break;
        }
        uint64_t first = osz == 8 ? rd64(z->c_buf) : rd32(z->c_buf);
        uint64_t nblob = first / (uint64_t)osz;
        if (nblob < 2 || (uint64_t)b + 2 > nblob) {
            break;  // (nblob offsets = nblob - 1 blobs)
        }
        r = cluster_more(z, (size_t)(b + 2) * (size_t)osz);
        if (r == 0) {
            continue;
        }
        if (r < 0) {
            break;
        }
        const uint8_t *ot = z->c_buf + (size_t)b * (size_t)osz;
        uint64_t o0 = osz == 8 ? rd64(ot) : rd32(ot), o1 = osz == 8 ? rd64(ot + osz) : rd32(ot + osz);
        if (o1 < o0) {
            break;
        }
        r = cluster_more(z, (size_t)o1);
        if (r == 0) {
            continue;
        }
        if (r < 0) {
            break;
        }
        *len = (uint32_t)(o1 - o0);
        return z->c_buf + o0;
    }
    zim_drop_cache(z);
    return NULL;
}
