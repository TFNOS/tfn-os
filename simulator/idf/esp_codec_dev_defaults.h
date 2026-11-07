// The simulator: the interfaces of the codec chips. Each "new" function gives a token that is not NULL; the chips
// have no registers here (sim_audio.c).
#pragma once
#include "esp_codec_dev.h"
#include "driver/i2s_std.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { uint8_t port; void *rx_handle; void *tx_handle; } audio_codec_i2s_cfg_t;
typedef struct { uint8_t port; uint8_t addr; void *bus_handle; } audio_codec_i2c_cfg_t;

const audio_codec_data_if_t *audio_codec_new_i2s_data(audio_codec_i2s_cfg_t *cfg);
const audio_codec_ctrl_if_t *audio_codec_new_i2c_ctrl(audio_codec_i2c_cfg_t *cfg);
const audio_codec_gpio_if_t *audio_codec_new_gpio(void);

#define ES8311_CODEC_DEFAULT_ADDR   (0x30)
#define ES7210_CODEC_DEFAULT_ADDR   (0x80)
#define AW88298_CODEC_DEFAULT_ADDR  (0x6C)
#define ES8388_CODEC_DEFAULT_ADDR   (0x20)
#define ES7210_SEL_MIC1             (1 << 0)
#define ES7210_SEL_MIC2             (1 << 1)
#define ES7210_SEL_MIC3             (1 << 2)
#define ES7210_SEL_MIC4             (1 << 3)

typedef struct {
    const audio_codec_ctrl_if_t *ctrl_if;
    const audio_codec_gpio_if_t *gpio_if;
    esp_codec_dec_work_mode_t codec_mode;
    int16_t pa_pin;
    bool pa_reverted;
    bool master_mode;
    bool use_mclk;
    bool digital_mic;
    bool invert_mclk;
    bool invert_sclk;
    esp_codec_dev_hw_gain_t hw_gain;
    bool no_dac_ref;
    uint16_t mclk_div;
} es8311_codec_cfg_t;
typedef struct {
    const audio_codec_ctrl_if_t *ctrl_if;
    bool master_mode;
    uint16_t mic_selected;
    uint16_t mclk_src;
    uint16_t mclk_div;
} es7210_codec_cfg_t;
typedef struct {
    const audio_codec_ctrl_if_t *ctrl_if;
    const audio_codec_gpio_if_t *gpio_if;
    int16_t reset_pin;
    esp_codec_dev_hw_gain_t hw_gain;
} aw88298_codec_cfg_t;
typedef struct {
    const audio_codec_ctrl_if_t *ctrl_if;
    const audio_codec_gpio_if_t *gpio_if;
    esp_codec_dec_work_mode_t codec_mode;
    bool master_mode;
    int16_t pa_pin;
    bool pa_reverted;
    esp_codec_dev_hw_gain_t hw_gain;
} es8388_codec_cfg_t;
typedef struct { const audio_codec_ctrl_if_t *ctrl_if; } es7243_codec_cfg_t;
typedef struct { const audio_codec_ctrl_if_t *ctrl_if; } es7243e_codec_cfg_t;

const audio_codec_if_t *es8311_codec_new(es8311_codec_cfg_t *cfg);
const audio_codec_if_t *es7210_codec_new(es7210_codec_cfg_t *cfg);
const audio_codec_if_t *aw88298_codec_new(aw88298_codec_cfg_t *cfg);
const audio_codec_if_t *es8388_codec_new(es8388_codec_cfg_t *cfg);
const audio_codec_if_t *es7243_codec_new(es7243_codec_cfg_t *cfg);
const audio_codec_if_t *es7243e_codec_new(es7243e_codec_cfg_t *cfg);

#ifdef __cplusplus
}
#endif
