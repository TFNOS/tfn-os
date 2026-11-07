// The web camera (see webcam.h). The CoreS3 has a GC0308 (VGA, RGB565 out, no JPEG in the
// sensor): the board encodes each picture to JPEG in software. The browser asks for one picture
// at a time (/api/cam.jpg), so the "video" works through any proxy (the manager, llm3).
//   GET /cam                        the page
//   GET /api/cam.jpg?q=10-95        one picture (320 x 240), image/jpeg
// 640 x 480 is not used: with the 8 KB DMA buffer (internal RAM) the picture data overflows, and
// the software JPEG takes about 0.8 s for each picture.
// The camera uses the system I2C bus of the board (SCCB on I2C_NUM_0, shared with the touch
// panel and the IMU) and its own clock (no XCLK pin).

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "web_util.h"
#include "tfn_util.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "board.h"
#include "gfx.h"
#include "webcam.h"
#include "wifi.h"

#define TAG     "webcam"
#define IDLE_MS 10000  // the camera stops this long after the last picture

static volatile int64_t s_last;  // the time of the last picture (0: never)

bool webcam_active(void)
{
    return s_last && tfn_now_ms() - s_last < IDLE_MS + 2000;
}

void webcam_overlay(void)
{
    if (!webcam_active()) {
        return;
    }
    int u = gfx_native() ? 2 : 1;
    const char *t = "CAMERA ON";
    int w = gfx_text_width(t, u) + 12 * u, h = 12 * u;
    int x = 4, y = 2;
#if BOARD_ROUND
    x = (gfx_w - w) / 2;  // the round panel: the middle of the top (the circle cuts the corners)
    y = gfx_round() ? 28 : 2;
#endif
    gfx_fill_rect(x, y, w, h, rgb(170, 30, 30));
    gfx_fill_circle(x + 4 * u, y + h / 2, 2 * u, rgb(255, 255, 255));
    gfx_text(x + 8 * u, y + (h - 7 * u) / 2, t, u, rgb(255, 255, 255));
}

static const char PAGE[] =
        "<style>"
        ".cam{max-width:900px;margin:1em auto;padding:0 1.2em 2em}"
        ".cam .bar{display:flex;flex-wrap:wrap;gap:.5em;align-items:center;margin-bottom:.8em}"
        ".cam .bar label{color:#8a93c0;font-size:.9em;display:flex;align-items:center;gap:.3em}"
        ".cam .bar .sp{flex:1}"
        ".cam .scr{position:relative;background:#000;border:1px solid #232850;border-radius:14px;overflow:hidden;"
        "aspect-ratio:4/3;display:flex;align-items:center;justify-content:center}"
        ".cam img{width:100%;height:100%;object-fit:contain;display:block}"
        ".cam .msg{position:absolute;color:#8a93c0;text-align:center;padding:1em}"
        ".cam .st{color:#8a93c0;font-size:.85em;margin-top:.5em}"
        ".cam .live{display:inline-block;width:9px;height:9px;border-radius:50%;background:#555;margin-right:.4em}"
        ".cam .live.on{background:#ff4a4a;box-shadow:0 0 8px #ff4a4a}"
        "</style>"
        "<div class=cam><div class=bar>"
        "<button id=go onclick='toggle()'>Start</button>"
        "<label>Quality <select id=q><option value=40>Low</option><option value=65 selected>Normal</option><option "
        "value=85>High</option></select></label>"
        "<label><input type=checkbox id=rot onchange='tf()'> Turn 180</label>"
        "<label><input type=checkbox id=mir onchange='tf()'> Mirror</label>"
        "<span class=sp></span><button onclick='snap()'>Save picture</button></div>"
        "<div class=scr><img id=im alt=''><div class=msg id=m>Push Start to see the camera.</div></div>"
        "<div class=st><span class=live id=lv></span><span id=st>Stopped.</span></div>"
        "<p class=st>The board shows CAMERA ON while a page gets pictures. The camera stops 10 s after the last "
        "picture.</p></div>"
        "<script>"
        "let on=false,n=0,t0=0,url=null,busy=false;const $=i=>document.getElementById(i);"
        "try{$('rot').checked=localStorage.camRot=='1';$('mir').checked=localStorage.camMir=='1'}catch(e){}"
        "function tf(){$('im').style.transform=($('rot').checked?'rotate(180deg) "
        "':'')+($('mir').checked?'scaleX(-1)':'');"
        "try{localStorage.camRot=$('rot').checked?'1':'0';localStorage.camMir=$('mir').checked?'1':'0'}catch(e){}}tf();"
        "function toggle(){on=!on;$('go').textContent=on?'Stop':'Start';$('lv').className='live'+(on?' on':'');"
        "if(on){n=0;t0=performance.now();$('st').textContent='Starting the camera...';next()}else "
        "$('st').textContent='Stopped.'}"
        "function next(){if(!on||busy)return;if(document.hidden){setTimeout(next,500);return}busy=true;"
        "fetch('api/cam.jpg?q='+$('q').value+'&t='+Date.now(),{cache:'no-store'}).then(r=>{"
        "if(!r.ok)return r.text().then(x=>{throw new Error(x||r.status)});return r.blob()}).then(b=>{"
        "const "
        "u=URL.createObjectURL(b);$('im').src=u;if(url)URL.revokeObjectURL(url);url=u;$('m').style.display='none';n++;"
        "const s=(performance.now()-t0)/1000;if(s>2){$('st').textContent=(n/s).toFixed(1)+' pictures/s, "
        "'+Math.round(b.size/1024)+' KB each';n=0;t0=performance.now()}"
        "busy=false;next()}).catch(e=>{busy=false;$('m').style.display='block';$('m').textContent='No picture: "
        "'+e.message;"
        "if(String(e.message).indexOf('no camera')>=0){toggle();return}setTimeout(next,2000)})}"
        "function snap(){if(!url)return;const a=document.createElement('a');a.href=url;"
        "a.download='camera-'+new Date().toISOString().slice(0,19).replace(/[:T]/g,'-')+'.jpg';a.click()}"
        "</script></body></html>";

static esp_err_t page_get(httpd_req_t *req)
{
    return web_send_page(req, PAGE);
}

#if BOARD_CAMERA == 1

#include "esp_camera.h"
#include "img_converters.h"

void board_camera_power(bool on);  // board_cores3.c: the camera power line (off while no picture is needed)

static SemaphoreHandle_t s_mx;
static esp_timer_handle_t s_idle;
static bool s_on, s_absent;

bool webcam_supported(void)
{
    return !s_absent;
}

static void cam_stop(void)
{
    if (!s_on) {
        return;
    }
    esp_camera_deinit();
    board_camera_power(false);
    s_on = false;
    ESP_LOGI(TAG, "camera off");
}

static void idle_cb(void *arg)
{
    (void)arg;
    if (xSemaphoreTake(s_mx, pdMS_TO_TICKS(2000)) != pdTRUE) {
        esp_timer_start_once(s_idle, 2000000);  // a picture is on the way: try again later
        return;
    }
    if (tfn_now_ms() - s_last >= IDLE_MS - 100) {
        cam_stop();
    }
    xSemaphoreGive(s_mx);
}

static esp_err_t cam_start(void)
{
    if (s_on) {
        return ESP_OK;
    }
    camera_config_t c = {
            .pin_pwdn = -1,
            .pin_reset = -1,
            .pin_xclk = -1,      // the camera module has its own clock
            .pin_sccb_sda = -1,  // use the I2C bus of the board (sccb_i2c_port)
            .pin_sccb_scl = -1,
            .sccb_i2c_port = 0,
            .pin_d7 = 47,
            .pin_d6 = 48,
            .pin_d5 = 16,
            .pin_d4 = 15,
            .pin_d3 = 42,
            .pin_d2 = 41,
            .pin_d1 = 40,
            .pin_d0 = 39,
            .pin_vsync = 46,
            .pin_href = 38,
            .pin_pclk = 45,
            .xclk_freq_hz = 20000000,
            .ledc_timer = LEDC_TIMER_0,
            .ledc_channel = LEDC_CHANNEL_0,
            .pixel_format = PIXFORMAT_RGB565,
            .frame_size = FRAMESIZE_QVGA,
            .jpeg_quality = 0,
            .fb_count = 2,
            .fb_location = CAMERA_FB_IN_PSRAM,
            .grab_mode = CAMERA_GRAB_LATEST,
    };
    int64_t t = tfn_now_ms();
    board_camera_power(true);
    esp_err_t e = esp_camera_init(&c);
    if (e != ESP_OK) {
        board_camera_power(false);
        ESP_LOGW(TAG, "camera start failed: %s", esp_err_to_name(e));
        if (e == ESP_ERR_CAMERA_NOT_DETECTED) {
            s_absent = true;  // the CoreS3-SE: no camera
        }
        return e;
    }
    s_on = true;
    ESP_LOGI(TAG, "camera on in %d ms, internal RAM free %u", (int)(tfn_now_ms() - t),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    return ESP_OK;
}

static esp_err_t api_jpg(httpd_req_t *req)
{
    char qs[64], v[8];
    int q = 65;
    if (httpd_req_get_url_query_str(req, qs, sizeof qs) == ESP_OK) {
        if (httpd_query_key_value(qs, "q", v, sizeof v) == ESP_OK) {
            q = atoi(v);
        }
    }
    if (q < 10) {
        q = 10;
    }
    if (q > 95) {
        q = 95;
    }
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    if (s_absent) {
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_set_type(req, "text/plain");
        return httpd_resp_sendstr(req, "no camera on this board");
    }
    xSemaphoreTake(s_mx, portMAX_DELAY);
    esp_err_t e = cam_start();
    uint8_t *jpg = NULL;
    size_t len = 0;
    if (e == ESP_OK) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (fb) {
            if (!frame2jpg(fb, (uint8_t)q, &jpg, &len)) {
                jpg = NULL;
            }
            esp_camera_fb_return(fb);
        }
        s_last = tfn_now_ms();
        esp_timer_stop(s_idle);
        esp_timer_start_once(s_idle, IDLE_MS * 1000LL);
    }
    xSemaphoreGive(s_mx);
    if (!jpg) {
        char m[80];
        snprintf(m, sizeof m, e == ESP_ERR_CAMERA_NOT_DETECTED ? "no camera on this board" : "camera error (%s)",
                 esp_err_to_name(e == ESP_OK ? ESP_FAIL : e));
        httpd_resp_set_status(req, e == ESP_ERR_CAMERA_NOT_DETECTED ? "404 Not Found" : "503 Service Unavailable");
        httpd_resp_set_type(req, "text/plain");
        return httpd_resp_sendstr(req, m);
    }
    httpd_resp_set_type(req, "image/jpeg");
    esp_err_t r = httpd_resp_send(req, (const char *)jpg, len);
    free(jpg);
    return r;
}

void webcam_register(void *h)
{
    if (!s_mx) {
        s_mx = xSemaphoreCreateMutex();
        const esp_timer_create_args_t a = {.callback = idle_cb, .name = "cam_idle"};
        esp_timer_create(&a, &s_idle);
    }
    httpd_uri_t u[] = {
            {.uri = "/cam", .method = HTTP_GET, .handler = page_get},
            {.uri = "/api/cam.jpg", .method = HTTP_GET, .handler = api_jpg},
    };
    for (size_t i = 0; i < sizeof u / sizeof u[0]; i++) {
        httpd_register_uri_handler(h, &u[i]);
    }
}

void webcam_console(const char *line)
{
    (void)line;
    ESP_LOGI(TAG, "wcam: this board has no AI chip (the camera is on the board)");
}

#elif BOARD_CAMERA == 2

// The SenseCAP Watcher: the camera (OV5647) is on the Himax WiseEye2 AI chip. The ESP asks the chip for one picture
// at a time with the SSCMA protocol (the facts come from Seeed's sscma_client, Apache-2.0): AT commands over SPI,
// and the chip answers with JSON lines "\r{...}\n"; a picture is a JPEG in base64 ("data": {"image": "..."}).
// Each SPI step is a packet of 256 bytes: 0x10, the command, the length (high byte first), the data, 0xFF 0xFF,
// zeros. WRITE (2) carries at most 250 bytes of text; AVAILABLE (3) is followed by a read of 2 bytes (the count of
// bytes ready); READ (1) is followed by a read of that count (at most 4095). The chip needs 2 ms before each step,
// and it has data only while its SYNC line is high (board_ai_sync()).
// The sensor modes (AT+SENSOR=1,1,opt): 0 = 240 x 240, 1 = 416 x 416, 2 = 480 x 480, 3 = 640 x 480.
#include "driver/spi_master.h"

esp_err_t board_ai_spi(spi_device_handle_t *dev);
bool board_ai_sync(void);
void board_ai_reset(void);
void board_ai_power(bool on);

#define T_READ  0x01
#define T_WRITE 0x02
#define T_AVAIL 0x03
#define PKT     256
#define RD_MAX  4095
#define RX_MAX  (320 * 1024)  // the replies (a 640 x 480 picture in base64 is about 40-120 KB)

static SemaphoreHandle_t s_mx;
static esp_timer_handle_t s_idle;
static spi_device_handle_t s_dev;
static uint8_t *s_dma;  // PKT + RD_MAX bytes in internal RAM (DMA), only while the link is open
static char *s_rx;      // the replies, in PSRAM
static int s_rx_n;
static bool s_absent, s_print;  // s_print: the console prints each reply
static int s_opt = -1;          // the sensor mode now (-1: the sensor is off)

bool webcam_supported(void)
{
    return !s_absent;
}

static esp_err_t xfer(const void *tx, void *rx, int n)
{
    spi_transaction_t t = {
            .length = (size_t)n * 8,
            .rxlength = rx ? (size_t)n * 8 : 0,
            .tx_buffer = tx,
            .rx_buffer = rx,
    };
    return spi_device_transmit(s_dev, &t);
}

// one command packet
static esp_err_t pkt(uint8_t cmd, int len, const void *data, int dlen)
{
    memset(s_dma, 0, PKT);
    s_dma[0] = 0x10;
    s_dma[1] = cmd;
    s_dma[2] = (uint8_t)(len >> 8);
    s_dma[3] = (uint8_t)len;
    if (dlen) {
        memcpy(s_dma + 4, data, dlen);
    }
    s_dma[4 + dlen] = 0xFF;
    s_dma[5 + dlen] = 0xFF;
    vTaskDelay(pdMS_TO_TICKS(2));
    return xfer(s_dma, NULL, PKT);
}

// the count of bytes that the chip has for the ESP (-1: an SPI error)
static int avail(void)
{
    if (!board_ai_sync()) {
        return 0;
    }
    if (pkt(T_AVAIL, 0, NULL, 0) != ESP_OK) {
        return -1;
    }
    vTaskDelay(pdMS_TO_TICKS(2));
    if (xfer(NULL, s_dma, 2) != ESP_OK) {
        return -1;
    }
    int n = s_dma[0] << 8 | s_dma[1];
    return n == 0xFFFF ? 0 : n;
}

// read n bytes (at most RD_MAX) to the end of s_rx (the chip pads with zero bytes: they are not kept)
static esp_err_t rx_read(int n)
{
    if (pkt(T_READ, n, NULL, 0) != ESP_OK) {
        return ESP_FAIL;
    }
    vTaskDelay(pdMS_TO_TICKS(2));
    if (xfer(NULL, s_dma, n) != ESP_OK) {
        return ESP_FAIL;
    }
    if (s_rx_n + n >= RX_MAX) {
        s_rx_n = 0;  // too much text with no end of a reply: start again
    }
    for (int i = 0; i < n; i++) {
        if (s_dma[i]) {
            s_rx[s_rx_n++] = (char)s_dma[i];
        }
    }
    s_rx[s_rx_n] = 0;
    return ESP_OK;
}

static esp_err_t at_send(const char *s)
{
    int n = (int)strlen(s);
    for (int i = 0; i < n; i += 250) {
        int k = n - i < 250 ? n - i : 250;
        if (pkt(T_WRITE, k, s + i, k) != ESP_OK) {
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

// the value of "key" in the JSON text j: a pointer to its first character (after the colon and the spaces)
static const char *json_val(const char *j, const char *key)
{
    char k[24];
    snprintf(k, sizeof k, "\"%s\"", key);
    const char *p = strstr(j, k);
    if (!p) {
        return NULL;
    }
    p += strlen(k);
    while (*p == ' ' || *p == ':') {
        p++;
    }
    return p;
}

// a reply {"type": 0, "name": "SAMPLE", "code": 0, "data": ...}: type 0 = the answer to a command, 1 = an event,
// 2 = a log. The name of the answer is the command with no "AT+" and no "=..." ("ID?", "SENSOR", "SAMPLE").
static bool reply_is(const char *j, const char *name, int type)
{
    const char *t = json_val(j, "type"), *n = json_val(j, "name");
    if (!t || !n || *n != '"' || atoi(t) != type) {
        return false;
    }
    int len = (int)strlen(name);
    return !strncmp(n + 1, name, len) && n[1 + len] == '"';
}

// Read the replies until one has this name and type (then it is at s_rx: "{...}" and its length is s_rx_n), or
// until ms go by (NULL). The other replies are dropped (the console prints them when s_print is on).
static char *wait_reply(const char *name, int type, int ms)
{
    int64_t end = tfn_now_ms() + ms;
    for (;;) {
        char *e;
        while ((e = strstr(s_rx, "}\n")) != NULL) {
            char *b = strstr(s_rx, "\r{");
            int used = (int)(e + 2 - s_rx);
            if (b && b < e) {
                e[1] = 0;  // the reply: b + 1 .. e
                if (s_print) {
                    int len = (int)(e - b);
                    ESP_LOGI(TAG, "wcam: %.300s%s (%d bytes)", b + 1, len > 300 ? "..." : "", len);
                }
                if (name && reply_is(b + 1, name, type)) {
                    s_rx_n = (int)(e - b);
                    memmove(s_rx, b + 1, (size_t)s_rx_n + 1);
                    return s_rx;
                }
            }
            memmove(s_rx, s_rx + used, (size_t)(s_rx_n - used) + 1);
            s_rx_n -= used;
        }
        if (tfn_now_ms() > end) {
            return NULL;
        }
        int n = avail();
        if (n < 0) {
            return NULL;
        }
        if (n == 0) {
            vTaskDelay(pdMS_TO_TICKS(3));
            continue;
        }
        while (n > 0) {
            int k = n > RD_MAX ? RD_MAX : n;
            if (rx_read(k) != ESP_OK) {
                return NULL;
            }
            n -= k;
        }
    }
}

// send a command and wait for its answer; true when its "code" is 0
static bool at_cmd(const char *cmd, const char *name, int ms)
{
    s_rx_n = 0;
    s_rx[0] = 0;
    for (int i = 0, n; i < 8 && (n = avail()) > 0; i++) {  // old data of the chip: away
        rx_read(n > RD_MAX ? RD_MAX : n);
    }
    s_rx_n = 0;
    s_rx[0] = 0;
    if (at_send(cmd) != ESP_OK) {
        return false;
    }
    const char *r = wait_reply(name, 0, ms);
    const char *c = r ? json_val(r, "code") : NULL;
    return c && atoi(c) == 0;
}

static bool link_open(void)
{
    if (s_dma) {
        return true;
    }
    if (!s_dev) {
        board_ai_power(true);
        if (board_ai_spi(&s_dev) != ESP_OK) {
            board_ai_power(false);
            return false;
        }
    }
    s_dma = heap_caps_malloc(PKT + RD_MAX + 1, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    s_rx = heap_caps_malloc(RX_MAX + 1, MALLOC_CAP_SPIRAM);
    if (!s_dma || !s_rx) {
        free(s_dma);
        free(s_rx);
        s_dma = NULL;
        s_rx = NULL;
        return false;
    }
    s_rx_n = 0;
    s_rx[0] = 0;
    return true;
}

static void link_close(void)
{
    free(s_dma);
    free(s_rx);
    s_dma = NULL;
    s_rx = NULL;
    if (s_dev) {  // the chip off (idle power): the SPI device away first (its CS pin)
        spi_bus_remove_device(s_dev);
        s_dev = NULL;
    }
    board_ai_power(false);
}

static void cam_stop(void)
{
    if (!s_dma) {
        return;
    }
    if (s_opt >= 0) {
        char c[40];
        snprintf(c, sizeof c, "AT+SENSOR=1,0,%d\r\n", s_opt);
        at_cmd(c, "SENSOR", 1000);
        s_opt = -1;
    }
    link_close();
    ESP_LOGI(TAG, "camera off");
}

static void stop_task(void *arg)
{
    (void)arg;
    if (xSemaphoreTake(s_mx, pdMS_TO_TICKS(3000)) == pdTRUE) {
        if (tfn_now_ms() - s_last >= IDLE_MS - 100) {
            cam_stop();
        }
        xSemaphoreGive(s_mx);
    }
    vTaskDelete(NULL);
}

static void idle_cb(void *arg)
{
    (void)arg;
    // the stop sends a command to the chip (up to 1 s): not in the timer task
    if (xTaskCreate(stop_task, "cam_stop", 3072, NULL, 2, NULL) != pdPASS) {
        esp_timer_start_once(s_idle, 2000000);
    }
}

// the link, the chip and the sensor in this mode
static esp_err_t cam_start(int opt)
{
    if (!link_open()) {
        return ESP_ERR_NO_MEM;
    }
    if (s_opt == opt) {
        return ESP_OK;
    }
    int64_t t = tfn_now_ms();
    if (s_opt < 0 && !at_cmd("AT+ID?\r\n", "ID?", 1000)) {
        ESP_LOGW(TAG, "the AI chip does not answer: a reset");
        board_ai_reset();
        if (!at_cmd("AT+ID?\r\n", "ID?", 3000)) {
            link_close();
            return ESP_ERR_TIMEOUT;
        }
    }
    char c[40];
    snprintf(c, sizeof c, "AT+SENSOR=1,1,%d\r\n", opt);
    if (!at_cmd(c, "SENSOR", 3000)) {
        ESP_LOGW(TAG, "the sensor mode %d: no", opt);
        return ESP_ERR_NOT_SUPPORTED;
    }
    s_opt = opt;
    ESP_LOGI(TAG, "camera on (mode %d) in %d ms, internal RAM free %u", opt, (int)(tfn_now_ms() - t),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    return ESP_OK;
}

// base64 to bytes; o can be the same buffer as s (the output is always behind the input)
static int b64_dec(const char *s, int n, uint8_t *o)
{
    static int8_t T[256];
    if (!T['B']) {
        memset(T, -1, sizeof T);
        const char *a = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (int i = 0; i < 64; i++) {
            T[(uint8_t)a[i]] = (int8_t)i;
        }
    }
    uint32_t v = 0;
    int bits = 0, k = 0;
    for (int i = 0; i < n; i++) {
        int c = T[(uint8_t)s[i]];
        if (c < 0) {
            continue;
        }
        v = (v << 6 | (uint32_t)c) & 0xFFFFFF;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            o[k++] = (uint8_t)(v >> bits);
        }
    }
    return k;
}

// one picture: the JPEG at s_rx, its length (0: none)
static int take_picture(void)
{
    s_rx_n = 0;
    s_rx[0] = 0;
    if (at_send("AT+SAMPLE=1\r\n") != ESP_OK) {
        return 0;
    }
    const char *r = wait_reply("SAMPLE", 1, 4000);
    const char *im = r ? json_val(r, "image") : NULL;
    if (!im || *im != '"') {
        return 0;
    }
    const char *q = strchr(im + 1, '"');
    if (!q) {
        return 0;
    }
    int n = b64_dec(im + 1, (int)(q - im - 1), (uint8_t *)s_rx);
    // the chip pads the JPEG with zero bytes: the picture ends at the last end mark (FF D9)
    const uint8_t *j = (const uint8_t *)s_rx;
    for (int k = n; k > 4 && k > n - 4096; k--) {
        if (j[k - 2] == 0xFF && j[k - 1] == 0xD9) {
            return k;
        }
    }
    return n;
}

static esp_err_t api_jpg(httpd_req_t *req)
{
    char qs[64], v[8];
    int opt = 3;
    if (httpd_req_get_url_query_str(req, qs, sizeof qs) == ESP_OK &&
        httpd_query_key_value(qs, "res", v, sizeof v) == ESP_OK) {
        opt = atoi(v) & 3;
    }
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    if (s_absent) {
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_set_type(req, "text/plain");
        return httpd_resp_sendstr(req, "no camera on this board");
    }
    xSemaphoreTake(s_mx, portMAX_DELAY);
    esp_err_t e = cam_start(opt);
    if (e == ESP_ERR_NOT_SUPPORTED && opt != 1) {
        e = cam_start(opt = 1);  // 416 x 416: in all chip versions
    }
    int len = 0;
    if (e == ESP_OK) {
        int64_t t = tfn_now_ms();
        len = take_picture();
        if (len <= 0) {
            ESP_LOGW(TAG, "no picture from the AI chip in %d ms", (int)(tfn_now_ms() - t));
        }
        s_last = tfn_now_ms();
        esp_timer_stop(s_idle);
        esp_timer_start_once(s_idle, IDLE_MS * 1000LL);
    }
    esp_err_t r;
    if (len <= 0) {
        char m[80];
        snprintf(m, sizeof m, "camera error (%s)", esp_err_to_name(e == ESP_OK ? ESP_FAIL : e));
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_set_type(req, "text/plain");
        r = httpd_resp_sendstr(req, m);
    } else {
        httpd_resp_set_type(req, "image/jpeg");
        r = httpd_resp_send(req, s_rx, len);
    }
    xSemaphoreGive(s_mx);
    return r;
}

static void mx_init(void)
{
    if (s_mx) {
        return;
    }
    s_mx = xSemaphoreCreateMutex();
    const esp_timer_create_args_t a = {.callback = idle_cb, .name = "cam_idle"};
    esp_timer_create(&a, &s_idle);
}

void webcam_register(void *h)
{
    mx_init();
    httpd_uri_t u[] = {
            {.uri = "/cam", .method = HTTP_GET, .handler = page_get},
            {.uri = "/api/cam.jpg", .method = HTTP_GET, .handler = api_jpg},
    };
    for (size_t i = 0; i < sizeof u / sizeof u[0]; i++) {
        httpd_register_uri_handler(h, &u[i]);
    }
}

void webcam_console(const char *line)
{
    mx_init();
    xSemaphoreTake(s_mx, portMAX_DELAY);
    if (!link_open()) {
        ESP_LOGW(TAG, "wcam: no link (memory)");
    } else if (line && !strcmp(line, "reset")) {
        board_ai_reset();
        ESP_LOGI(TAG, "wcam: the AI chip is reset");
    } else if (line && !strcmp(line, "pic")) {
        int64_t t = tfn_now_ms();
        esp_err_t e = cam_start(s_opt >= 0 ? s_opt : 1);
        int len = e == ESP_OK ? take_picture() : 0;
        const uint8_t *j = (const uint8_t *)s_rx;
        ESP_LOGI(TAG, "wcam: %s, a picture of %d bytes in %d ms (%02X %02X ... %02X %02X)", esp_err_to_name(e), len,
                 (int)(tfn_now_ms() - t), len > 4 ? j[0] : 0, len > 4 ? j[1] : 0, len > 4 ? j[len - 2] : 0,
                 len > 4 ? j[len - 1] : 0);
        s_last = tfn_now_ms();
        esp_timer_stop(s_idle);
        esp_timer_start_once(s_idle, IDLE_MS * 1000LL);
    } else if (line && *line) {
        char c[200];
        snprintf(c, sizeof c, "%s\r\n", line);
        s_print = true;
        s_rx_n = 0;
        s_rx[0] = 0;
        ESP_LOGI(TAG, "wcam: send %s, SYNC %d", line, (int)board_ai_sync());
        if (at_send(c) == ESP_OK) {
            wait_reply(NULL, 0, 3000);
        }
        s_print = false;
        ESP_LOGI(TAG, "wcam: end");
    }
    if (s_opt < 0 && s_dma) {
        link_close();
    }
    xSemaphoreGive(s_mx);
}

#else  // the 3.49 and the AMOLED-1.8 have no camera

bool webcam_supported(void)
{
    return false;
}

void webcam_console(const char *line)
{
    (void)line;
    ESP_LOGI(TAG, "wcam: this board has no camera");
}

void webcam_register(void *h)
{
    (void)h;
    (void)page_get;
}

#endif
