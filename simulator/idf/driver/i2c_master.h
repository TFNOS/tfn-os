// The simulator: no I2C bus (the chips of the board are parts of the simulator). It is found before
// tests/host/common/driver/i2c_master.h and has the same handle type.
#pragma once
#include <stdint.h>
#include "esp_err.h"
typedef void *i2c_master_bus_handle_t;
typedef void *i2c_master_dev_handle_t;
typedef enum { I2C_NUM_0, I2C_NUM_1, I2C_NUM_MAX } i2c_port_t;
static inline esp_err_t i2c_master_probe(i2c_master_bus_handle_t bus, uint16_t addr, int timeout_ms)
{
    (void)bus; (void)addr; (void)timeout_ms;
    return ESP_ERR_NOT_FOUND;
}
