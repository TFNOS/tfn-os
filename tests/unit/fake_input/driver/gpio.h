// A small copy of the ESP-IDF GPIO driver for the input test (the touch INT line: the test board has none)
#pragma once
#include <stdint.h>
#include "esp_err.h"

#ifndef ESP_ERR_INVALID_STATE
#define ESP_ERR_INVALID_STATE 0x103
#endif

typedef int gpio_num_t;
typedef enum {
    GPIO_INTR_DISABLE,
    GPIO_INTR_LOW_LEVEL
} gpio_int_type_t;
typedef enum {
    GPIO_MODE_INPUT
} gpio_mode_t;
typedef enum {
    GPIO_PULLUP_DISABLE,
    GPIO_PULLUP_ENABLE
} gpio_pullup_t;
typedef struct {
    uint64_t pin_bit_mask;
    gpio_mode_t mode;
    gpio_pullup_t pull_up_en;
    gpio_int_type_t intr_type;
} gpio_config_t;
typedef void (*gpio_isr_t)(void *arg);

esp_err_t gpio_config(const gpio_config_t *c);
esp_err_t gpio_install_isr_service(int flags);
esp_err_t gpio_isr_handler_add(gpio_num_t pin, gpio_isr_t fn, void *arg);
esp_err_t gpio_set_intr_type(gpio_num_t pin, gpio_int_type_t type);
esp_err_t gpio_intr_enable(gpio_num_t pin);
esp_err_t gpio_intr_disable(gpio_num_t pin);
esp_err_t gpio_wakeup_enable(gpio_num_t pin, gpio_int_type_t type);
