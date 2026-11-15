// The simulator: the board (board.h) of build.py --frame: the Waveshare ESP32-S3-Touch-LCD-3.49, the 320 x 240 frame
// of the CoreS3, the round SenseCAP Watcher, the round AMOLED-1.75C, the AMOLED-1.8 or the Tab5. The capabilities of the board come from
// boards/<kind>/board_caps.h. The sim keeps THE PANEL (the physical picture, portrait) and puts each frame of the
// firmware on it as the driver of the board does (main/board_*.c):
// - the 3.49 and the CoreS3: the panel is the frame (LCD_W x LCD_H), turned from the logical frame as
//   gfx_touch_to_logical() defines it;
// - a board with the HI frame (the Tab5): the panel is the frame x 2; each frame pixel is 2 x 2 panel pixels (the CPU
//   way of board_tab5.c), a HI frame goes 1:1;
// - a round board with BOARD_ROUND_ZOOM (the Watcher): the panel is the square round the circle (BOARD_ROUND_HI); no
//   turn (BOARD_FIXED_ORIENT); a round layout (the native frame) goes 1:1; the other apps: the CoreS3 frame, zoomed
//   (bilinear, as board_watcher.c) to the width of the circle, or 1:1 in the middle (board_round_fit());
// - a round board with no zoom (the AMOLED-1.75C): the panel is BOARD_ROUND_HI x BOARD_ROUND_HI; the frames turn; the
//   round frame of the panel size goes 1:1, the native frame is scaled to the circle, the CoreS3 frame is scaled to
//   280 x 372 in the middle with THE AMBIENT BORDER round it (the same steps as board_am175.c).
// The window shows the panel in the position of the board (a round board: only the circle). The touch points come from
// the window as panel points; board_touch_points() gives them as the touch driver of the board gives them (physical
// points of the frame, or logical points on a BOARD_TOUCH_LOGICAL board). input.c reads them each 10 ms. The PWR and
// BOOT keys, the backlight, the battery and the SD card are values that the window sets and reads.
#include <string.h>
#include "board.h"
#include "gfx.h"
#include "esp_log.h"
#include "sim.h"

static const char *TAG = "board";

// ---------------------------------------------------------------- the panel of the board
#if BOARD_ROUND
#define PANEL_W BOARD_ROUND_HI
#define PANEL_H BOARD_ROUND_HI
#elif BOARD_NATIVE_W > 0
#define PANEL_W BOARD_NATIVE_W
#define PANEL_H BOARD_NATIVE_H
#elif BOARD_HI_FRAME
#define PANEL_W (LCD_W * 2)
#define PANEL_H (LCD_H * 2)
#else
#define PANEL_W LCD_W
#define PANEL_H LCD_H
#endif
// the AMOLED-1.8 (a native panel that is not round): the frame of the apps x 7/5 at the middle (board_amoled.c)
#define SIM_AM18   (BOARD_NATIVE_W > 0 && !BOARD_ROUND)
#define AM18_PIC_X ((BOARD_NATIVE_W - LCD_W * BOARD_NATIVE_H / LCD_H) / 2)
#define AM18_PIC_W (LCD_W * BOARD_NATIVE_H / LCD_H)
// the AMOLED-1.75C look: the CoreS3 frame at 280 x 372 in the middle of the circle, the ambient border round it
#define SIM_AMBIENT (BOARD_ROUND && !BOARD_ROUND_ZOOM)

static uint16_t s_panel[PANEL_W * PANEL_H];     // the physical picture, RGB565 in the byte order of the PC
static uint16_t s_phys[PANEL_W * PANEL_H];      // the frame of the firmware, turned to the physical frame
static sim_mutex_t s_panel_lock = SIM_MUTEX_INIT;
static volatile uint32_t s_frames;
static volatile int s_backlight;                // 0..255 (the board starts with the backlight off)
static volatile bool s_asleep, s_speaker = true, s_speaker_hold;
static volatile int s_fps = 60;                 // the most frames a second (the board: about 30..60)
static int64_t s_last_flush;
static volatile bool s_fit;                     // board_round_fit(): the CoreS3 frame at 1:1 (the Watcher)
static volatile int s_shown = -1;               // the orient (with the frame bits) of the last frame

static sim_mutex_t s_touch_lock = SIM_MUTEX_INIT;
static int s_tn, s_tx[5], s_ty[5];              // panel points
static volatile bool s_key[2];                  // PWR, BOOT
static volatile int s_batt = 82;
static volatile bool s_usb = true;
static volatile bool s_sd_mounted;

void board_init(void)
{
    ESP_LOGI(TAG, "the simulated board %s (a frame of %d x %d, a panel of %d x %d)", BOARD_KIND_NAME, LCD_W, LCD_H,
             PANEL_W, PANEL_H);
}
board_ver_t board_version(void) { return BOARD_V2; }
i2c_master_bus_handle_t board_i2c0(void) { return NULL; }

// The size of the frame of the firmware in portrait (physical) for the frame bits of an orient.
static void frame_size(int orient, int *pw, int *ph)
{
    *pw = LCD_W;
    *ph = LCD_H;
#if BOARD_NATIVE_W > 0
    if (orient & ORIENT_RHI) {
        *pw = *ph = BOARD_ROUND_HI;
    } else if (orient & ORIENT_NATIVE) {
        *pw = BOARD_NATIVE_W;
        *ph = BOARD_NATIVE_H;
    }
#endif
#if BOARD_HI_FRAME
    if (orient & ORIENT_HI) {
        *pw = LCD_W * 2;
        *ph = LCD_H * 2;
    }
#endif
}

// The logical size of a frame (the frame of the app: landscape = the physical size turned).
void sim_board_frame_size(int orient, int *w, int *h)
{
    int pw, ph;
    frame_size(orient, &pw, &ph);
    bool land = (orient & 3) == ORIENT_LAND_A || (orient & 3) == ORIENT_LAND_B;
    *w = land ? ph : pw;
    *h = land ? pw : ph;
}

// The frame to s_phys (RGB565 in the byte order of the PC): turned to the physical frame (pw x ph), or upright on a
// board with BOARD_FIXED_ORIENT (the logical frame as it is: *w x *h).
static void to_phys(const uint16_t *fb, int orient, int *w, int *h)
{
    int pw, ph;
    frame_size(orient, &pw, &ph);
    if (BOARD_FIXED_ORIENT) {
        sim_board_frame_size(orient, w, h);
        for (int i = 0; i < *w * *h; i++) s_phys[i] = (uint16_t)((fb[i] >> 8) | (fb[i] << 8));
        return;
    }
    *w = pw;
    *h = ph;
    for (int py = 0; py < ph; py++) {
        uint16_t *row = &s_phys[py * pw];
        for (int px = 0; px < pw; px++) {
            int i;
            switch (orient & 3) {
            case ORIENT_PORTRAIT:       i = py * pw + px; break;
            case ORIENT_PORTRAIT_FLIP:  i = (ph - 1 - py) * pw + (pw - 1 - px); break;
            case ORIENT_LAND_A:         i = (pw - 1 - px) * ph + py; break;
            default:                    i = px * ph + (ph - 1 - py); break;
            }
            uint16_t c = fb[i];
            row[px] = (uint16_t)((c >> 8) | (c << 8));      // (the frame has the bytes of the SPI bus)
        }
    }
}

// ---- the bilinear scale of the round boards (the same steps as board_watcher.c and board_am175.c)
// A colour in the "spread" form 00000GGGGGG00000RRRRR000000BBBBB has room for a weighted sum (weights 0..32).
#define SPREAD_MASK 0x07E0F81Fu
static inline uint32_t spread(uint16_t c) { return ((uint32_t)c | ((uint32_t)c << 16)) & SPREAD_MASK; }
static inline uint32_t mix(uint32_t a, uint32_t b, uint32_t w) { return ((a * (32 - w) + b * w) >> 5) & SPREAD_MASK; }
static inline uint16_t unspread(uint32_t v) { return (uint16_t)((v | (v >> 16)) & 0xFFFF); }

// for each output column (row): the first source column (row) and the weight of the next one
static void zoom_table(int16_t *idx, uint8_t *w, int out, int src)
{
    for (int i = 0; i < out; i++) {
        int p = ((2 * i + 1) * src * 32) / (2 * out) - 16;      // the centre of the output pixel, 1/32 px
        if (p < 0) p = 0;
        int k = p >> 5, f = p & 31;
        if (k >= src - 1) k = src - 2, f = 32;
        idx[i] = (int16_t)k;
        w[i] = (uint8_t)f;
    }
}

// s_phys (sw x sh) scaled to ow x oh at (ox, oy) of the panel
static void put_scaled(int sw, int sh, int ox, int oy, int ow, int oh)
{
    static int16_t zx[PANEL_W > PANEL_H ? PANEL_W : PANEL_H], zy[PANEL_W > PANEL_H ? PANEL_W : PANEL_H];
    static uint8_t wx[PANEL_W > PANEL_H ? PANEL_W : PANEL_H], wy[PANEL_W > PANEL_H ? PANEL_W : PANEL_H];
    static uint32_t ra[PANEL_W], rb[PANEL_W];
    zoom_table(zx, wx, ow, sw);
    zoom_table(zy, wy, oh, sh);
    for (int y = 0; y < oh; y++) {
        const uint16_t *a = &s_phys[zy[y] * sw], *b = a + sw;
        for (int x = 0; x < ow; x++) {      // (each source row scaled across, then the two rows mixed)
            int k = zx[x];
            ra[x] = mix(spread(a[k]), spread(a[k + 1]), wx[x]);
            rb[x] = mix(spread(b[k]), spread(b[k + 1]), wx[x]);
        }
        uint16_t *o = &s_panel[(oy + y) * PANEL_W + ox];
        for (int x = 0; x < ow; x++) o[x] = unspread(mix(ra[x], rb[x], wy[y]));
    }
}

// s_phys (sw x sh) at 1:1 (k = 1) or k x k (each pixel k x k times) at (ox, oy) of the panel
static void put_copy(int sw, int sh, int ox, int oy, int k)
{
    for (int y = 0; y < sh * k; y++) {
        const uint16_t *s = &s_phys[(y / k) * sw];
        uint16_t *o = &s_panel[(oy + y) * PANEL_W + ox];
        for (int x = 0; x < sw * k; x++) o[x] = s[x / k];
    }
}

#if SIM_AMBIENT
// ---- THE AMBIENT BORDER of board_am175.c: a grid of AG_W x AG_H cells of the physical CoreS3 frame (each 20 x 20 px;
// the mean of 16 samples), blurred two times (3 x 3) and mixed with the grid of the frames before (a quarter of the new
// one). A border point has the grid colour at the nearest point of the picture, darker away from the picture, and a
// thin dark line goes round the picture. (The board sends a quarter of the border in each frame; the sim draws all of
// it in each frame.)
#define PIC_X0  94
#define PIC_Y0  47
#define PIC_W   280
#define PIC_H   372
#define LINE_W  2
#define AG_W    12
#define AG_H    16
#define AG_CELL 20
static float s_ag[AG_H][AG_W][3];
static bool s_ag_ok;
static uint32_t s_top[PANEL_W], s_bot[PANEL_W], s_left[PIC_H], s_right[PIC_H];
static uint8_t s_fade_y[PANEL_H], s_fade_x[PANEL_W];    // 0..32: the strength at the distance

static inline uint32_t dim(uint32_t a, uint32_t w) { return ((a * w) >> 5) & SPREAD_MASK; }

static void ambient_grid(void)
{
    static float g[AG_H][AG_W][3], t[AG_H][AG_W][3];
    for (int cy = 0; cy < AG_H; cy++) {
        for (int cx = 0; cx < AG_W; cx++) {
            int r = 0, gg = 0, b = 0;
            for (int sy = 0; sy < 4; sy++) {
                const int py = cy * AG_CELL + 2 + sy * 5;
                for (int sx = 0; sx < 4; sx++) {
                    const int px = cx * AG_CELL + 2 + sx * 5;
                    const uint16_t c = s_phys[py * LCD_W + px];
                    r += c >> 11;
                    gg += (c >> 5) & 63;
                    b += c & 31;
                }
            }
            g[cy][cx][0] = r * (255.0f / 31 / 16);
            g[cy][cx][1] = gg * (255.0f / 63 / 16);
            g[cy][cx][2] = b * (255.0f / 31 / 16);
        }
    }
    for (int pass = 0; pass < 2; pass++) {
        for (int cy = 0; cy < AG_H; cy++) {
            for (int cx = 0; cx < AG_W; cx++) {
                for (int k = 0; k < 3; k++) {
                    float s = 0;
                    int n = 0;
                    for (int dy = -1; dy <= 1; dy++) {
                        for (int dx = -1; dx <= 1; dx++) {
                            const int y = cy + dy, x = cx + dx;
                            if (y < 0 || y >= AG_H || x < 0 || x >= AG_W) continue;
                            s += g[y][x][k];
                            n++;
                        }
                    }
                    t[cy][cx][k] = s / n;
                }
            }
        }
        memcpy(g, t, sizeof g);
    }
    const float keep = s_ag_ok ? 0.75f : 0.0f;
    for (int cy = 0; cy < AG_H; cy++)
        for (int cx = 0; cx < AG_W; cx++)
            for (int k = 0; k < 3; k++) s_ag[cy][cx][k] = s_ag[cy][cx][k] * keep + g[cy][cx][k] * (1 - keep);
    s_ag_ok = true;
}

// the grid colour at the physical frame point (fx, fy) (bilinear between the cell centres), in the spread form
static uint32_t ambient_at(float fx, float fy)
{
    float gx = fx / AG_CELL - 0.5f, gy = fy / AG_CELL - 0.5f;
    gx = gx < 0 ? 0 : gx > AG_W - 1 ? AG_W - 1 : gx;
    gy = gy < 0 ? 0 : gy > AG_H - 1 ? AG_H - 1 : gy;
    int x0 = (int)gx, y0 = (int)gy;
    if (x0 > AG_W - 2) x0 = AG_W - 2;
    if (y0 > AG_H - 2) y0 = AG_H - 2;
    const float ax = gx - x0, ay = gy - y0;
    float c[3];
    for (int k = 0; k < 3; k++) {
        const float a = s_ag[y0][x0][k] + (s_ag[y0][x0 + 1][k] - s_ag[y0][x0][k]) * ax;
        const float b = s_ag[y0 + 1][x0][k] + (s_ag[y0 + 1][x0 + 1][k] - s_ag[y0 + 1][x0][k]) * ax;
        c[k] = a + (b - a) * ay;
    }
    return spread((uint16_t)((((int)c[0] >> 3) << 11) | (((int)c[1] >> 2) << 5) | ((int)c[2] >> 3)));
}

// the frame position of a panel column (row) of the picture, clamped to the frame
static float frame_x(int x)
{
    float f = (x - PIC_X0 + 0.5f) * LCD_W / PIC_W;
    return f < 0 ? 0 : f > LCD_W - 1 ? LCD_W - 1 : f;
}
static float frame_y(int y)
{
    float f = (y - PIC_Y0 + 0.5f) * LCD_H / PIC_H;
    return f < 0 ? 0 : f > LCD_H - 1 ? LCD_H - 1 : f;
}

// the strength of the border at the distance from the picture: 0.62 next to it, 0.22 at the edge of the circle
static void fade_init(void)
{
    for (int i = 0; i < PANEL_W; i++) {
        const int dy = i < PIC_Y0 ? PIC_Y0 - i : i >= PIC_Y0 + PIC_H ? i - (PIC_Y0 + PIC_H - 1) : 0;
        const int dx = i < PIC_X0 ? PIC_X0 - i : i >= PIC_X0 + PIC_W ? i - (PIC_X0 + PIC_W - 1) : 0;
        const float fy = 0.62f - 0.40f * (dy / (float)PIC_Y0), fx = 0.62f - 0.40f * (dx / (float)PIC_X0);
        s_fade_y[i] = (uint8_t)(32 * (fy < 0.22f ? 0.22f : fy));
        s_fade_x[i] = (uint8_t)(32 * (fx < 0.22f ? 0.22f : fx));
    }
}

static void put_ambient(void)
{
    static bool s_init;
    if (!s_init) {
        fade_init();
        s_init = true;
    }
    ambient_grid();
    for (int x = 0; x < PANEL_W; x++) {
        s_top[x] = ambient_at(frame_x(x), 0);
        s_bot[x] = ambient_at(frame_x(x), LCD_H - 1);
    }
    for (int y = 0; y < PIC_H; y++) {
        s_left[y] = ambient_at(0, frame_y(PIC_Y0 + y));
        s_right[y] = ambient_at(LCD_W - 1, frame_y(PIC_Y0 + y));
    }
    put_scaled(LCD_W, LCD_H, PIC_X0, PIC_Y0, PIC_W, PIC_H);
    for (int y = 0; y < PANEL_H; y++) {
        uint16_t *o = &s_panel[y * PANEL_W];
        const bool line_row = (y >= PIC_Y0 - LINE_W && y < PIC_Y0) ||
                              (y >= PIC_Y0 + PIC_H && y < PIC_Y0 + PIC_H + LINE_W);
        const bool pic_row = y >= PIC_Y0 && y < PIC_Y0 + PIC_H;
        for (int px = 0; px < PANEL_W; px++) {
            const bool line_col = px >= PIC_X0 - LINE_W && px < PIC_X0 + PIC_W + LINE_W;
            if (pic_row) {
                if (px >= PIC_X0 && px < PIC_X0 + PIC_W) continue;      // (the picture)
                o[px] = line_col ? 0 : unspread(dim(px < PIC_X0 ? s_left[y - PIC_Y0] : s_right[y - PIC_Y0],
                                                     s_fade_x[px]));
            } else if (line_row && line_col) {
                o[px] = 0;
            } else {
                const uint32_t fy = s_fade_y[y];
                o[px] = unspread(dim(y < PIC_Y0 ? s_top[px] : s_bot[px], fy < s_fade_x[px] ? fy : s_fade_x[px]));
            }
        }
    }
}
#endif

// The frame of the firmware on the panel, as the driver of the board puts it there.
static void compose(const uint16_t *fb, int orient)
{
    int sw, sh;
    to_phys(fb, orient, &sw, &sh);
#if BOARD_ROUND && BOARD_ROUND_ZOOM
    if (orient & ORIENT_NATIVE) {
        put_copy(sw, sh, 0, 0, 1);
        return;
    }
    memset(s_panel, 0, sizeof s_panel);         // (the bands outside the picture: black)
    if (s_fit) {
        put_copy(sw, sh, (PANEL_W - sw) / 2, (PANEL_H - sh) / 2, 1);
        return;
    }
    const int big = sw > sh ? sw : sh, ow = sw * PANEL_W / big, oh = sh * PANEL_H / big;
    put_scaled(sw, sh, (PANEL_W - ow) / 2, (PANEL_H - oh) / 2, ow, oh);
#elif SIM_AMBIENT
    if (sw == PANEL_W) {
        put_copy(sw, sh, 0, 0, 1);              // (the round frame of the panel size)
    } else if (orient & ORIENT_NATIVE) {
        put_scaled(sw, sh, 0, 0, PANEL_W, PANEL_H);
    } else {
        put_ambient();
    }
#elif SIM_AM18
    // board_amoled.c: the native frame goes 1:1; the frame of the apps is scaled by 7/5, with black bands at the sides
    if (orient & ORIENT_NATIVE) {
        put_copy(sw, sh, 0, 0, 1);
    } else {
        memset(s_panel, 0, sizeof s_panel);
        put_scaled(sw, sh, AM18_PIC_X, 0, AM18_PIC_W, PANEL_H);
    }
#else
    put_copy(sw, sh, 0, 0, PANEL_W / sw);
#endif
}

void board_lcd_flush(const uint16_t *fb, int orient)
{
    sim_mutex_lock(&s_panel_lock);
#if SIM_AMBIENT
    if (orient != s_shown) {
        s_ag_ok = false;                        // (a new mode or orientation: the border starts from this frame)
    }
#endif
    compose(fb, orient);
    s_shown = orient;
    s_frames++;
    sim_mutex_unlock(&s_panel_lock);
    int fw, fh;
    sim_board_frame_size(orient, &fw, &fh);
    sim_rec_frame(fb, fw, fh);
    // the time of the transfer: at most s_fps frames a second
    int64_t frame_us = 1000000 / (s_fps > 0 ? s_fps : 60), now = sim_now_us();
    if (now - s_last_flush < frame_us) sim_sleep_us(frame_us - (now - s_last_flush));
    now = sim_now_us();
    s_last_flush = now - s_last_flush > 2 * frame_us ? now : s_last_flush + frame_us;
}

void board_backlight(uint8_t level) { s_backlight = level; }
void board_speaker(bool on) { s_speaker = on; }
void board_speaker_hold(bool hold) { s_speaker_hold = hold; }
void board_screen_sleep(bool sleep) { s_asleep = sleep; }

// ---------------------------------------------------------------- the touch
// A panel point to the point that the touch driver of the board gives; false = no touch (outside the picture). held:
// the finger was on the picture in the last read (board_am175.c: it then follows the finger anywhere on the glass).
static bool touch_point(int tx, int ty, int *x, int *y, bool held)
{
#if BOARD_ROUND && BOARD_ROUND_ZOOM
    (void)held;
    // board_watcher.c: logical points of the frame now (the native frame = the panel; the zoomed CoreS3 frame = the
    // inverse zoom; the fit frame = the panel point less the offset of the frame)
    bool native = gfx_native(), land = orient_is_landscape(gfx_orient());
    int sw = native ? PANEL_W : land ? LCD_H : LCD_W, sh = native ? PANEL_H : land ? LCD_W : LCD_H;
    int big = sw > sh ? sw : sh, ow = native ? PANEL_W : sw * PANEL_W / big, oh = native ? PANEL_H : sh * PANEL_H / big;
    if (!native && s_fit) ow = sw, oh = sh;
    int ox = (PANEL_W - ow) / 2, oy = (PANEL_H - oh) / 2;
    int lx = (tx - ox) * sw / ow, ly = (ty - oy) * sh / oh;
    *x = lx < 0 ? 0 : lx >= sw ? sw - 1 : lx;
    *y = ly < 0 ? 0 : ly >= sh ? sh - 1 : ly;
    return true;
#elif SIM_AMBIENT
    // board_am175.c panel_to_logical(): the scale, the turn and the border of the frame of the last flush
    const int shown = s_shown;
    if (shown < 0) return false;
    int W, H, px, py;
    if (shown & ORIENT_RHI) {
        W = H = PANEL_W;
        px = tx;
        py = ty;
    } else if (shown & ORIENT_NATIVE) {
        W = H = BOARD_NATIVE_W;
        px = (tx * W + W / 2) / PANEL_W;
        py = (ty * H + H / 2) / PANEL_H;
    } else {
        W = LCD_W;
        H = LCD_H;
        px = ((tx - PIC_X0) * LCD_W + LCD_W / 2) / PIC_W;
        py = ((ty - PIC_Y0) * LCD_H + LCD_H / 2) / PIC_H;
        const int slack = 24;       // (a finger on the border near the picture is a touch at its edge)
        if (!held && (px < -slack || px >= W + slack || py < -slack || py >= H + slack)) return false;
    }
    px = px < 0 ? 0 : px >= W ? W - 1 : px;
    py = py < 0 ? 0 : py >= H ? H - 1 : py;
    switch (shown & 0xFF) {
    case ORIENT_PORTRAIT:       *x = px; *y = py; break;
    case ORIENT_PORTRAIT_FLIP:  *x = W - 1 - px; *y = H - 1 - py; break;
    case ORIENT_LAND_A:         *x = py; *y = W - 1 - px; break;
    default:                    *x = H - 1 - py; *y = px; break;
    }
    return true;
#elif SIM_AM18
    // board_amoled.c: the physical points of the frame of the apps (the bands at the sides: its edge)
    (void)held;
    const int px = (tx - AM18_PIC_X) * LCD_W / AM18_PIC_W, py = ty * LCD_H / PANEL_H;
    *x = px < 0 ? 0 : px >= LCD_W ? LCD_W - 1 : px;
    *y = py < 0 ? 0 : py >= LCD_H ? LCD_H - 1 : py;
    return true;
#else
    // the physical points of the frame (a board with the HI frame: the panel is the frame x 2, board_tab5.c)
    (void)held;
    *x = tx * LCD_W / PANEL_W;
    *y = ty * LCD_H / PANEL_H;
    return true;
#endif
}

static int s_prev_n;     // the points of the last read
int board_touch_points(int *x, int *y, int max)
{
    sim_mutex_lock(&s_touch_lock);
    int n = 0;
    for (int i = 0; i < s_tn && n < max; i++) {
        if (touch_point(s_tx[i], s_ty[i], &x[n], &y[n], s_prev_n > n)) n++;
    }
    s_prev_n = n;
    sim_mutex_unlock(&s_touch_lock);
    return n;
}

void board_touch_log(bool on) { (void)on; }
int board_touch_reports(touch_rep_t *out, int max) { (void)out; (void)max; return 0; }
void board_touch_read_len(int n) { (void)n; }
int board_touch_read_len_get(void) { return 32; }
void board_tp_diag(int secs) { (void)secs; }
int board_tp_reg(int reg, int val) { (void)reg; (void)val; return -1; }
int board_touch_int_gpio(void) { return -1; }
void board_touch_int_ack(void) {}

bool board_pwr_key_down(void) { return s_key[0]; }
bool board_boot_key_down(void) { return s_key[1]; }
int board_wheel_pos(void) { return 0; }
void board_round_fit(bool on) { s_fit = on; }
int board_wheel_raw(void) { return 0; }

void board_power_off(void)
{
    ESP_LOGI(TAG, "power off");
    s_backlight = 0;
    sim_set_state(SIM_OFF, "board_power_off()");
    sim_park();
}

int board_battery(bool *charging, int *mv)
{
    int pct = s_batt;
    if (charging) *charging = s_usb && pct < 100;
    if (mv) *mv = s_usb ? 4150 : 3350 + pct * 8;
    return pct;
}

bool board_ohm_start(void) { return false; }        // (the 3.49 has no free pins for OHMS)
void board_ohm_stop(void) {}
bool board_ohm_read(int *mv) { (void)mv; return false; }

bool board_sd_mount(void)
{
    s_sd_mounted = sim_fs_sd_is_present();
    return s_sd_mounted;
}
bool board_sd_check(void)
{
    if (!sim_fs_sd_is_present()) s_sd_mounted = false;
    return s_sd_mounted;
}
bool board_sd_mounted(void) { return s_sd_mounted && sim_fs_sd_is_present(); }
bool board_sd_info(uint32_t *total_kb, uint32_t *free_kb)
{
    if (!board_sd_mounted()) return false;
    if (total_kb) *total_kb = 15558144;                 // (a card of 16 GB)
    if (free_kb) *free_kb = 15200000;
    return true;
}

// ---------------------------------------------------------------- the window side
void sim_board_touch(int n, const int *x, const int *y)
{
    if (n > 5) n = 5;
    sim_mutex_lock(&s_touch_lock);
    s_tn = n < 0 ? 0 : n;
    for (int i = 0; i < s_tn; i++) {
        s_tx[i] = x[i] < 0 ? 0 : x[i] >= PANEL_W ? PANEL_W - 1 : x[i];
        s_ty[i] = y[i] < 0 ? 0 : y[i] >= PANEL_H ? PANEL_H - 1 : y[i];
    }
    sim_mutex_unlock(&s_touch_lock);
}

void sim_board_key(int key, bool down)
{
    if (key == 0 || key == 1) s_key[key] = down;
}

uint32_t sim_board_frames(void) { return s_frames; }
int sim_board_backlight(void) { return s_backlight; }
bool sim_board_asleep(void) { return s_asleep; }
bool sim_board_speaker_on(void) { return s_speaker || s_speaker_hold; }
void sim_board_battery(int pct, bool usb) { s_batt = pct < 0 ? -1 : pct > 100 ? 100 : pct; s_usb = usb; }
void sim_board_fps(int fps) { s_fps = fps < 5 ? 5 : fps > 240 ? 240 : fps; }

// The pose of the view: a board with BOARD_FIXED_ORIENT shows each frame upright (the view is the panel).
int sim_board_view_pose(int orient) { return BOARD_FIXED_ORIENT ? ORIENT_PORTRAIT : orient; }

// The picture of the panel: the panel in the position of the board (pose: an orient_t; the same turn as the logical
// frame of that orientation), width px wide (the height keeps the shape; each pixel of the picture takes the nearest
// pixel of the panel), RGB 8:8:8, each value times k / 256 (the backlight). A round board: black outside the circle.
static int panel_rgb(uint8_t *rgb, int cap, int pose, int width, int k, int *w, int *h)
{
    const int W = PANEL_W, H = PANEL_H;
    pose = sim_board_view_pose(pose);
    bool land = pose == ORIENT_LAND_A || pose == ORIENT_LAND_B;
    int vw = land ? H : W, vh = land ? W : H;
    if (width < vw / 4) width = vw / 4;
    *w = width;
    *h = (vh * width + vw / 2) / vw;
    if (cap < *w * *h * 3) return 0;
    static uint8_t lut[3][64];
    static int lut_k = -1;
    if (lut_k != k) {
        for (int v = 0; v < 64; v++) {
            int r5 = v & 31, g6 = v;
            lut[0][v] = (uint8_t)(((r5 << 3) | (r5 >> 2)) * k / 256);
            lut[1][v] = (uint8_t)(((g6 << 2) | (g6 >> 4)) * k / 256);
        }
        lut_k = k;
    }
    static int xs[4096];
    for (int x = 0; x < *w && x < 4096; x++) xs[x] = x * vw / *w;
    sim_mutex_lock(&s_panel_lock);
    int last_vy = -1;
    for (int y = 0; y < *h; y++) {
        uint8_t *out = rgb + (size_t)y * *w * 3;
        int vy = y * vh / *h;
        if (vy == last_vy) {                            // (the same row of the panel: a copy)
            memcpy(out, out - (size_t)*w * 3, (size_t)*w * 3);
            continue;
        }
        last_vy = vy;
        for (int x = 0; x < *w && x < 4096; x++) {
            int vx = xs[x], px, py;
            switch (pose) {
            case ORIENT_PORTRAIT:       px = vx; py = vy; break;
            case ORIENT_PORTRAIT_FLIP:  px = W - 1 - vx; py = H - 1 - vy; break;
            case ORIENT_LAND_A:         px = W - 1 - vy; py = vx; break;
            default:                    px = vy; py = H - 1 - vx; break;
            }
            uint16_t c = s_panel[py * W + px];
#if BOARD_ROUND
            // (the glass is a circle: the integer test of the centre of each pixel, 2x scale)
            int dx = 2 * px + 1 - W, dy = 2 * py + 1 - H;
            if (dx * dx + dy * dy > W * W) c = 0;
#endif
            *out++ = lut[0][(c >> 11) & 31];
            *out++ = lut[1][(c >> 5) & 63];
            *out++ = lut[0][c & 31];
        }
    }
    sim_mutex_unlock(&s_panel_lock);
    return *w * *h * 3;
}

// The picture for the window: with the backlight (0 = black; a low level is still readable on the board)
int sim_board_view(uint8_t *rgb, int cap, int pose, int width, int *w, int *h)
{
    int bl = s_asleep ? 0 : s_backlight;
    int k = bl <= 0 ? 0 : 40 + bl * 216 / 255;
    return panel_rgb(rgb, cap, pose, width, k, w, h);
}

// The picture for a test: each pixel of the panel once, the colours of the frame with no backlight
int sim_board_shot(uint8_t *rgb, int cap, int pose, int *w, int *h)
{
    pose = sim_board_view_pose(pose);
    bool land = pose == ORIENT_LAND_A || pose == ORIENT_LAND_B;
    return panel_rgb(rgb, cap, pose, land ? PANEL_H : PANEL_W, 256, w, h);
}

// the physical panel (portrait): the touch points of the window are points of it
void sim_board_lcd(int *w, int *h)
{
    *w = PANEL_W;
    *h = PANEL_H;
}
