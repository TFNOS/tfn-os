// QR codes on the screen, from the espressif/qrcode component (see qr.h).

#include <stdio.h>
#include <string.h>
#include "qrcode.h"
#include "gfx.h"
#include "qr.h"

#define MAX_N  41  // version 6
#define BORDER 2

static char s_text[160];
static uint8_t s_mod[MAX_N * MAX_N];
static int s_n;

// the component calls this during the generation: keep the modules
static void capture(esp_qrcode_handle_t q)
{
    int n = esp_qrcode_get_size(q);
    if (n > MAX_N) {
        s_n = 0;
        return;
    }
    for (int y = 0; y < n; y++) {
        for (int x = 0; x < n; x++) {
            s_mod[y * n + x] = esp_qrcode_get_module(q, x, y);
        }
    }
    s_n = n;
}

static bool make(const char *text)
{
    if (!text || strlen(text) >= sizeof(s_text)) {
        return false;
    }
    if (!strcmp(text, s_text)) {
        return s_n > 0;
    }
    snprintf(s_text, sizeof(s_text), "%s", text);
    s_n = 0;
    esp_qrcode_config_t cfg = {
            .display_func = capture,
            .max_qrcode_version = 6,
            .qrcode_ecc_level = ESP_QRCODE_ECC_LOW,
    };
    esp_qrcode_generate(&cfg, text);
    return s_n > 0;
}

int qr_side(int px, const char *text)
{
    return make(text) ? (s_n + 2 * BORDER) * px : 0;
}

int qr_draw(int x, int y, int px, const char *text)
{
    if (!make(text)) {
        return 0;
    }
    int side = (s_n + 2 * BORDER) * px;
    gfx_fill_rect(x, y, side, side, rgb(255, 255, 255));
    int ox = x + BORDER * px, oy = y + BORDER * px;
    for (int r = 0; r < s_n; r++) {
        // dark runs of a row as one rectangle
        int c = 0;
        while (c < s_n) {
            if (!s_mod[r * s_n + c]) {
                c++;
                continue;
            }
            int c0 = c;
            while (c < s_n && s_mod[r * s_n + c]) {
                c++;
            }
            gfx_fill_rect(ox + c0 * px, oy + r * px, (c - c0) * px, px, rgb(0, 0, 0));
        }
    }
    return side;
}
