// The simulator: the codec device of esp_codec_dev (the same names and types that main/audio.c uses). A device of
// type OUT plays on the sound card of the PC; a device of type IN gives the made-up microphone (sim_audio.c).
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ESP_CODEC_DEV_OK            (0)
#define ESP_CODEC_DEV_DRV_ERR       (-1)
#define ESP_CODEC_DEV_INVALID_ARG   (-2)
#define ESP_CODEC_DEV_NOT_SUPPORT   (-3)
#define ESP_CODEC_DEV_NOT_FOUND     (-4)
#define ESP_CODEC_DEV_WRONG_STATE   (-5)
#define ESP_CODEC_DEV_WRITE_FAIL    (-6)
#define ESP_CODEC_DEV_READ_FAIL     (-7)

typedef enum {
    ESP_CODEC_DEV_TYPE_NONE,
    ESP_CODEC_DEV_TYPE_IN = (1 << 0),
    ESP_CODEC_DEV_TYPE_OUT = (1 << 1),
    ESP_CODEC_DEV_TYPE_IN_OUT = (ESP_CODEC_DEV_TYPE_IN | ESP_CODEC_DEV_TYPE_OUT),
} esp_codec_dev_type_t;

typedef enum {
    ESP_CODEC_DEV_WORK_MODE_NONE,
    ESP_CODEC_DEV_WORK_MODE_ADC = (1 << 0),
    ESP_CODEC_DEV_WORK_MODE_DAC = (1 << 1),
    ESP_CODEC_DEV_WORK_MODE_BOTH = (ESP_CODEC_DEV_WORK_MODE_ADC | ESP_CODEC_DEV_WORK_MODE_DAC),
    ESP_CODEC_DEV_WORK_MODE_LINE = (1 << 2),
} esp_codec_dec_work_mode_t;

typedef struct {
    uint8_t bits_per_sample;
    uint8_t channel;
    uint16_t channel_mask;
    uint32_t sample_rate;
    int mclk_multiple;
} esp_codec_dev_sample_info_t;

typedef struct { float pa_voltage; float codec_dac_voltage; float pa_gain; } esp_codec_dev_hw_gain_t;

typedef struct audio_codec_if_s { int kind; } audio_codec_if_t;
typedef struct audio_codec_data_if_s { int kind; } audio_codec_data_if_t;
typedef struct audio_codec_ctrl_if_s { int kind; } audio_codec_ctrl_if_t;
typedef struct audio_codec_gpio_if_s { int kind; } audio_codec_gpio_if_t;

typedef struct {
    esp_codec_dev_type_t dev_type;
    const audio_codec_if_t *codec_if;
    const audio_codec_data_if_t *data_if;
} esp_codec_dev_cfg_t;

typedef void *esp_codec_dev_handle_t;

esp_codec_dev_handle_t esp_codec_dev_new(esp_codec_dev_cfg_t *cfg);
int esp_codec_dev_open(esp_codec_dev_handle_t dev, esp_codec_dev_sample_info_t *fs);
int esp_codec_dev_close(esp_codec_dev_handle_t dev);
void esp_codec_dev_delete(esp_codec_dev_handle_t dev);
int esp_codec_dev_write(esp_codec_dev_handle_t dev, void *data, int len);
int esp_codec_dev_read(esp_codec_dev_handle_t dev, void *data, int len);
int esp_codec_dev_set_out_vol(esp_codec_dev_handle_t dev, int volume);
int esp_codec_dev_get_out_vol(esp_codec_dev_handle_t dev, int *volume);
int esp_codec_dev_set_out_mute(esp_codec_dev_handle_t dev, bool mute);
int esp_codec_dev_set_in_gain(esp_codec_dev_handle_t dev, float db);
int esp_codec_dev_set_in_channel_gain(esp_codec_dev_handle_t dev, uint16_t mask, float db);
int esp_codec_dev_set_in_mute(esp_codec_dev_handle_t dev, bool mute);

#ifdef __cplusplus
}
#endif
