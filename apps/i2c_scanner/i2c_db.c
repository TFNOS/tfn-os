// I2C SCANNER: the data base of the usual chips at each address (i2c_db.h). An entry with an ID register: the chip
// gives the value idval at the register idreg (a WHO_AM_I or CHIP_ID register; the data sheets of the chips).
// The task: the app task and the bus task read it. The memory: a constant table in the app file.
#include "i2c_db.h"

const i2c_chip_t I2C_DB[] = {
        {0x0C, 0x0C, 0x00, 0x48, true, "AK8963 (A COMPASS)"},
        {0x0D, 0x0D, 0x0D, 0xFF, true, "QMC5883L (A COMPASS)"},
        {0x10, 0x10, 0, 0, false, "VEML7700 (A LIGHT SENSOR)"},
        {0x10, 0x11, 0, 0, false, "ES8388 (A SOUND CODEC)"},
        {0x13, 0x14, 0, 0, false, "ES7243 (A MICROPHONE ADC)"},
        {0x14, 0x14, 0, 0, false, "GT911 (A TOUCH CHIP)"},
        {0x18, 0x18, 0xFD, 0x83, true, "ES8311 (A SOUND CODEC)"},
        {0x18, 0x19, 0x0F, 0x33, true, "LIS3DH (AN ACCELEROMETER)"},
        {0x18, 0x1F, 0, 0, false, "MCP9808 (A THERMOMETER)"},
        {0x1E, 0x1E, 0x0A, 0x48, true, "HMC5883L (A COMPASS)"},
        {0x20, 0x27, 0, 0, false, "PCF8574 / MCP23017 / TCA9554 (AN IO EXPANDER)"},
        {0x23, 0x23, 0, 0, false, "BH1750 (A LIGHT SENSOR)"},
        {0x23, 0x23, 0x86, 0x92, true, "LTR-553ALS (A LIGHT SENSOR)"},
        {0x29, 0x29, 0xC0, 0xEE, true, "VL53L0X (A DISTANCE SENSOR)"},
        {0x29, 0x29, 0x92, 0x44, true, "TCS34725 (A COLOUR SENSOR)"},
        {0x34, 0x34, 0x03, 0x4A, true, "AXP2101 (A POWER CHIP)"},
        {0x34, 0x34, 0, 0, false, "AXP192 (A POWER CHIP)"},
        {0x36, 0x36, 0, 0, false, "MAX17048 (A BATTERY GAUGE) / AW88298 (AN AMPLIFIER)"},
        {0x38, 0x38, 0, 0, false, "AHT10 / AHT20 (TEMPERATURE AND HUMIDITY)"},
        {0x38, 0x38, 0, 0, false, "FT6336 / FT3168 (A TOUCH CHIP)"},
        {0x38, 0x3F, 0, 0, false, "PCF8574A (AN IO EXPANDER)"},
        {0x39, 0x39, 0, 0, false, "APDS-9960 (A GESTURE SENSOR)"},
        {0x3C, 0x3D, 0, 0, false, "SSD1306 / SH1106 (AN OLED SCREEN)"},
        {0x40, 0x40, 0, 0, false, "INA219 (A POWER MONITOR) / HTU21D / Si7021 / PCA9685"},
        {0x41, 0x45, 0, 0, false, "INA219 / INA226 (A POWER MONITOR)"},
        {0x44, 0x45, 0, 0, false, "SHT3x / SHT4x (TEMPERATURE AND HUMIDITY)"},
        {0x48, 0x4B, 0, 0, false, "ADS1115 (AN ADC) / TMP102 / LM75 (A THERMOMETER)"},
        {0x50, 0x57, 0, 0, false, "24Cxx (AN EEPROM)"},
        {0x51, 0x51, 0, 0, false, "PCF8563 / BM8563 / PCF85063 (A CLOCK)"},
        {0x53, 0x53, 0x00, 0xE5, true, "ADXL345 (AN ACCELEROMETER)"},
        {0x57, 0x57, 0xFF, 0x15, true, "MAX30102 (A PULSE SENSOR)"},
        {0x58, 0x5B, 0x10, 0x23, true, "AW9523B (AN IO EXPANDER)"},
        {0x5A, 0x5B, 0x20, 0x81, true, "CCS811 (AN AIR SENSOR)"},
        {0x5A, 0x5A, 0, 0, false, "MLX90614 (AN IR THERMOMETER) / MPR121 (TOUCH KEYS)"},
        {0x5C, 0x5D, 0, 0, false, "AM2320 / LPS22 / MPR121"},
        {0x60, 0x60, 0, 0, false, "MCP4725 (A DAC) / Si1145 / ATECC608 / MPL3115A2"},
        {0x61, 0x62, 0, 0, false, "SCD30 / SCD4x (A CO2 SENSOR)"},
        {0x68, 0x69, 0x75, 0x68, true, "MPU-6050 (AN IMU)"},
        {0x68, 0x69, 0x75, 0x70, true, "MPU-6500 (AN IMU)"},
        {0x68, 0x69, 0x75, 0x71, true, "MPU-9250 (AN IMU)"},
        {0x68, 0x69, 0x00, 0x24, true, "BMI270 (AN IMU)"},
        {0x68, 0x69, 0x00, 0xD1, true, "BMI160 (AN IMU)"},
        {0x68, 0x68, 0, 0, false, "DS3231 / DS1307 (A CLOCK)"},
        {0x6A, 0x6B, 0x0F, 0x69, true, "LSM6DS3 (AN IMU)"},
        {0x6A, 0x6B, 0x0F, 0x6C, true, "LSM6DSO (AN IMU)"},
        {0x6A, 0x6B, 0x00, 0x05, true, "QMI8658 (AN IMU)"},
        {0x70, 0x77, 0, 0, false, "TCA9548A (AN I2C SWITCH) / HT16K33 (A LED DRIVER)"},
        {0x76, 0x77, 0xD0, 0x58, true, "BMP280 (A BAROMETER)"},
        {0x76, 0x77, 0xD0, 0x60, true, "BME280 (PRESSURE, TEMPERATURE, HUMIDITY)"},
        {0x76, 0x77, 0xD0, 0x61, true, "BME680 (AN AIR SENSOR)"},
        {0x76, 0x77, 0x00, 0x50, true, "BMP388 (A BAROMETER)"},
        {0x76, 0x77, 0x00, 0x60, true, "BMP390 (A BAROMETER)"},
        {0x77, 0x77, 0, 0, false, "MS5611 (A BAROMETER)"},
};
const int I2C_DB_N = (int)(sizeof I2C_DB / sizeof I2C_DB[0]);

bool i2c_db_risky(uint8_t a)
{
    // a write of one byte to an IO expander (PCF8574 0x20-0x27, PCF8574A 0x38-0x3F) sets its outputs, and to an I2C
    // switch (TCA9548A 0x70-0x77) its channels: the app reads the ID registers there only when the user asks. (0x76
    // and 0x77 stay free: there a barometer (BMP280, BME280) is much more usual than a switch with all address pins
    // set.)
    return (a >= 0x20 && a <= 0x27) || (a >= 0x38 && a <= 0x3F) || (a >= 0x70 && a <= 0x75);
}
