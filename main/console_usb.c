// The console commands of USB: the sound card, the phones, MIDI (the table: console.c).
#include "console_priv.h"

static const char *TAG = "console";

// "usbaudio"
void con_usbaudio(char *cmd)
{
    // a test of USB AUDIO: usbaudio <seconds> [mic|board|both]: the console port goes away (USB AUDIO has the
    // PHY); with the sound of the board a tone of 440 Hz plays each second; then the console port comes back
    // and the result is logged
    char *a = strtok(NULL, " \t\r\n"), *b = strtok(NULL, " \t\r\n");
    int secs = a ? atoi(a) : 10;
    if (secs < 3) {
        secs = 3;
    }
    if (secs > 120) {
        secs = 120;
    }
    int src = !b || !strcmp(b, "board") ? UA_SRC_BOARD : !strcmp(b, "mic") ? UA_SRC_MIC : UA_SRC_MIC | UA_SRC_BOARD;
    ESP_LOGI(TAG, "usbaudio: the console goes away for %d s (source %d)", secs, src);
    vTaskDelay(pdMS_TO_TICKS(200));
    bool was = usb_audio_state() != UA_OFF;
    usb_audio_open(src);
    int64_t t0 = esp_timer_get_time(), t_link = 0, t_stream = 0;
    int st_max = 0, k = 0;
    float lvl_max = 0;
    while (esp_timer_get_time() - t0 < (int64_t)secs * 1000000) {
        ua_state_t st = usb_audio_state();
        if ((int)st > st_max && st != UA_FAILED) {
            st_max = (int)st;
        }
        if (st == UA_LINKED && !t_link) {
            t_link = esp_timer_get_time();
        }
        if (usb_audio_streaming() && !t_stream) {
            t_stream = esp_timer_get_time();
        }
        if (usb_audio_level() > lvl_max) {
            lvl_max = usb_audio_level();
        }
        if ((src & UA_SRC_BOARD) && k % 50 == 0) {
            sfx_tone(440, 440, 400, W_SINE, 60);
        }
        k++;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (!was) {
        usb_audio_close();
    }
    vTaskDelay(pdMS_TO_TICKS(4000));  // the computer finds the console port again
    ESP_LOGI(TAG,
             "usbaudio: best state %d (2 = linked) after %d ms, stream after %d ms, sent %lu, dropped %lu, level "
             "max %.3f",
             st_max, t_link ? (int)((t_link - t0) / 1000) : -1, t_stream ? (int)((t_stream - t0) / 1000) : -1,
             (unsigned long)usb_audio_sent(), (unsigned long)usb_audio_dropped(), lvl_max);
}

// "usbphones"
void con_usbphones(char *cmd)
{
    // a test of USB-C HEADPHONES (the CoreS3): usbphones <seconds>: 5 V out, the USB host waits for a USB audio
    // device; a tone of 440 Hz plays each second; the console port goes away until the end
    char *a = strtok(NULL, " \t\r\n");
    int secs = a ? atoi(a) : 20;
    if (secs < 5) {
        secs = 5;
    }
    if (secs > 120) {
        secs = 120;
    }
    if (!usb_phones_supported()) {
        ESP_LOGI(TAG, "usbphones: this board has no 5 V out on its USB-C");
        return;
    }
    ESP_LOGI(TAG, "usbphones: the console goes away for %d s", secs);
    vTaskDelay(pdMS_TO_TICKS(200));
    bool was = usb_phones_state() != UP_OFF;
    usb_phones_open();
    int64_t t0 = esp_timer_get_time(), t_play = 0;
    int st_max = 0, k = 0;
    char name[32] = "";
    while (esp_timer_get_time() - t0 < (int64_t)secs * 1000000) {
        up_state_t st = usb_phones_state();
        if ((int)st > st_max) {
            st_max = (int)st;
        }
        if (st == UP_PLAYING && !t_play) {
            t_play = esp_timer_get_time();
            snprintf(name, sizeof name, "%s", usb_phones_name());
        }
        if (k % 50 == 0) {
            sfx_tone(440, 440, 400, W_SINE, 60);
        }
        k++;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (!was) {
        usb_phones_close();
    }
    vTaskDelay(pdMS_TO_TICKS(4000));  // the computer finds the console port again
    ESP_LOGI(TAG, "usbphones: best state %d (2 = playing, 3 = failed), playing after %d ms, the device \"%s\"", st_max,
             t_play ? (int)((t_play - t0) / 1000) : -1, name);
}

// "usbmidi"
void con_usbmidi(char *cmd)
{
    // a test of USB MIDI: the console port goes away for N seconds (USB MIDI has the PHY),
    // a CC 20 sweep goes out, then the console port comes back and the result is logged
    char *a = strtok(NULL, " \t\r\n");
    int secs = a ? atoi(a) : 10;
    if (secs < 3) {
        secs = 3;
    }
    if (secs > 60) {
        secs = 60;
    }
    ESP_LOGI(TAG, "usbmidi: the console goes away for %d s", secs);
    vTaskDelay(pdMS_TO_TICKS(200));
    usb_midi_open();
    int64_t t0 = esp_timer_get_time(), t_link = 0;
    int st_max = 0, v = 0;
    while (esp_timer_get_time() - t0 < (int64_t)secs * 1000000) {
        um_state_t st = usb_midi_state();
        if ((int)st > st_max) {
            st_max = (int)st;
        }
        if (st == UM_LINKED && !t_link) {
            t_link = esp_timer_get_time();
        }
        uint8_t m[3] = {0xB0, 20, (uint8_t)(v++ & 127)};
        usb_midi_send(m, 3);
        usb_midi_poll();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    usb_midi_close();
    vTaskDelay(pdMS_TO_TICKS(4000));  // the computer finds the console port again
    ESP_LOGI(TAG, "usbmidi: best state %d (2 = linked) after %d ms, sent %lu, received %lu", st_max,
             t_link ? (int)((t_link - t0) / 1000) : -1, (unsigned long)usb_midi_sent(),
             (unsigned long)usb_midi_received());
}
