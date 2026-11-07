// The I2C helpers of the board files (inside the OS). Each board file gets its own static copy. They are not
// "inline": with that word GCC puts add_dev() inline in board_init() and the code grows. "unused": a file can use
// only some of them.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

#define I2C_HZ         400000  // the clock of each device on the board bus
#define I2C_TIMEOUT_MS 50

// One register write: the register number, then the value.
static __attribute__((unused)) esp_err_t reg_write(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t val)
{
    uint8_t b[2] = {reg, val};
    return i2c_master_transmit(dev, b, sizeof(b), I2C_TIMEOUT_MS);
}

// A read of len bytes from the register reg on.
static __attribute__((unused)) esp_err_t reg_read(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *val, size_t len)
{
    return i2c_master_transmit_receive(dev, &reg, 1, val, len, I2C_TIMEOUT_MS);
}

// A device with a 7-bit address on the bus, at I2C_HZ. A fault stops the start of the board (ESP_ERROR_CHECK).
static __attribute__((unused)) i2c_master_dev_handle_t add_dev(i2c_master_bus_handle_t bus, uint16_t addr)
{
    i2c_device_config_t dc = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = addr,
            .scl_speed_hz = I2C_HZ,
    };
    i2c_master_dev_handle_t dev;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &dc, &dev));
    return dev;
}
