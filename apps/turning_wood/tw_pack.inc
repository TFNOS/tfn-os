// THE TURNING WOOD: the data pack on the SD card (TW_DIR). A picture (TWP1): w, h, a palette of RGB565 colours,
// the indices (one byte a pixel) packed with zlib. A layer or a sprite (TWL1): x, y, w, h, a palette (index 0 =
// clear), the indices. The files are made by the paint tools bake.py and closeups.py. Included by app_wood.c.
#if BOARD_UI_TABLET
// (the Tab5: all files of the app in the Tab5 pack (the store data "wood_t5"; the keeper of the manager
// gives it to a Tab5 in place of the normal pack): the pictures of 640 x 360 (the same scenes with a taller view)
// and a copy of the other files)
#ifndef TW_DIR
#define TW_DIR "/sdcard/wood_t5/"
#endif
#endif
#ifndef TW_DIR
#define TW_DIR "/sdcard/wood/"
#endif
#if BOARD_UI_TABLET
// a file of a Tab5 picture (a place, a layer, a close-up, a document): its layers come in the rows of the 640 x 360
// picture
static bool pk_t5(const char *name)
{
    return name[1] == '_' && strchr("plc", name[0]);
}
#endif

typedef struct {
    int16_t x, y, w, h;  // the place of the sprite in its picture (a close-up, a place: 640 x 240)
    uint16_t *lut;       // the colours of the indices (the byte order of the frame buffer)
    uint8_t *ix;         // w x h colour indices: 0 = clear (1 byte a pixel: the 24 ring pictures of
                         // the Great Wheel take 0.6 MB, not 1.8 MB)
} tw_spr_t;

static int s_pack_err;  // the files that did not load (the log names each one)

// the whole file into PSRAM (free it with heap_caps_free)
static uint8_t *pk_read(const char *name, size_t *len)
{
    char path[96];
    snprintf(path, sizeof path, TW_DIR "%s", name);
    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGW(TAG, "no file %s", path);
        s_pack_err++;
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = n > 0 ? heap_caps_malloc((size_t)n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : NULL;
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) {
        heap_caps_free(b);
        b = NULL;
    }
    fclose(f);
    if (!b) {
        ESP_LOGW(TAG, "cannot read %s", path);
        s_pack_err++;
        return NULL;
    }
    *len = (size_t)n;
    return b;
}
static inline uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}
static inline uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

// inflate zlib data into out (n bytes); true when all of it came out
static bool pk_inflate(const uint8_t *z, size_t zn, uint8_t *out, size_t n)
{
    tinfl_decompressor *d = malloc(sizeof(tinfl_decompressor));
    if (!d) {
        return false;
    }
    tinfl_init(d);
    size_t in = zn, got = n;
    const int st = tinfl_decompress(d, z, &in, out, out, &got,
                                    TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    free(d);
    return st == TINFL_STATUS_DONE && got == n;
}

// a picture into dst (dst_w x dst_h, the colours in the byte order of the frame buffer); idx: a scratch of w x h bytes
static bool pk_picture(const char *name, uint16_t *dst, int dst_w, int dst_h, uint8_t *idx)
{
    size_t len;
    uint8_t *b = pk_read(name, &len);
    if (!b) {
        return false;
    }
    bool ok = false;
    if (len > 10 && !memcmp(b, "TWP1", 4)) {
        const int w = rd16(b + 4), h = rd16(b + 6), n = rd16(b + 8);
        const uint8_t *pal = b + 10, *zp = pal + n * 2;
        const uint32_t zn = rd32(zp);
        if (w == dst_w && h == dst_h && n <= 256 && (size_t)(zp + 4 - b) + zn <= len &&
            pk_inflate(zp + 4, zn, idx, (size_t)w * h)) {
            uint16_t lut[256];
            for (int i = 0; i < n; i++) {
                const uint16_t c = rd16(pal + i * 2);
                lut[i] = (uint16_t)(c >> 8 | c << 8);
            }
            for (int i = 0; i < w * h; i++) {
                dst[i] = lut[idx[i]];
            }
            ok = true;
        }
    }
    if (!ok) {
        ESP_LOGW(TAG, "bad picture %s", name);
        s_pack_err++;
    }
    heap_caps_free(b);
    return ok;
}

static void spr_free(tw_spr_t *s)
{
    heap_caps_free(s->lut);
    heap_caps_free(s->ix);
    memset(s, 0, sizeof *s);
}
static bool pk_sprite(const char *name, tw_spr_t *s)
{
    memset(s, 0, sizeof *s);
    size_t len;
    uint8_t *b = pk_read(name, &len);
    if (!b) {
        return false;
    }
    bool ok = false;
    if (len > 14 && !memcmp(b, "TWL1", 4)) {
        s->x = (int16_t)rd16(b + 4);
        s->y = (int16_t)rd16(b + 6);
        s->w = (int16_t)rd16(b + 8);
        s->h = (int16_t)rd16(b + 10);
        const int n = rd16(b + 12);
        const uint8_t *pal = b + 14, *zp = pal + n * 2;
        const uint32_t zn = rd32(zp);
        const size_t np = (size_t)s->w * s->h;
        s->lut = heap_caps_calloc(256, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        s->ix = heap_caps_malloc(np, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s->lut && s->ix && n <= 256 && (size_t)(zp + 4 - b) + zn <= len && pk_inflate(zp + 4, zn, s->ix, np)) {
            for (int i = 0; i < n; i++) {
                const uint16_t c = rd16(pal + i * 2);
                s->lut[i] = (uint16_t)(c >> 8 | c << 8);
            }
            ok = true;
#if BOARD_UI_TABLET
            if (pk_t5(name)) {
                s->y -= T5Y;  // (a layer of a Tab5 picture: in the rows of the old picture)
            }
#endif
        }
    }
    heap_caps_free(b);
    if (!ok) {
        ESP_LOGW(TAG, "bad sprite %s", name);
        s_pack_err++;
        spr_free(s);
    }
    return ok;
}

// a sprite into a picture buffer (stride bw) at its own place, moved by (dx, dy)
static void spr_to_buf(const tw_spr_t *s, uint16_t *buf, int bw, int bh, int dx, int dy)
{
    if (!s->ix) {
        return;
    }
    for (int y = 0; y < s->h; y++) {
        const int by = s->y + dy + y;
        if (by < 0 || by >= bh) {
            continue;
        }
        for (int x = 0; x < s->w; x++) {
            const int bx = s->x + dx + x;
            const uint8_t k = s->ix[y * s->w + x];
            if (bx < 0 || bx >= bw || !k) {
                continue;
            }
            buf[by * bw + bx] = s->lut[k];
        }
    }
}
// a sprite onto the frame: its picture position (sx, sy) minus the view (vx, vy)
static void spr_to_fb(const tw_spr_t *s, int ox, int oy)
{
    spr_to_buf(s, g_fb, SW, SH, ox, oy);
}
