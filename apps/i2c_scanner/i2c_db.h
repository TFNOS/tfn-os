#pragma once
// I2C SCANNER: the data base of the usual chips at each 7-bit address (i2c_db.c).
// The task: the app task and the bus task read it. The memory: a constant table in the app file.
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t lo, hi;        // the addresses of the chip (its address pins)
    uint8_t idreg, idval;  // an ID register and its value
    bool has_id;
    const char *name;
} i2c_chip_t;

extern const i2c_chip_t I2C_DB[];
extern const int I2C_DB_N;
// true: a write of one byte can change the device (an IO expander, an I2C switch): no ID read unless the user asks
bool i2c_db_risky(uint8_t addr);
