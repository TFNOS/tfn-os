// RFPULSE: the wiring data of the board Waveshare ESP32-S3-Touch-AMOLED-1.8:
// the pads, the sockets and the I2C buses that the radio and bus apps show. libs/rfpulse/rfp_board.c includes
// this file (BOARD_RFP_WIRING of board_caps.h); the facts come from docs/headers.md.
static const rfp_conn_t CONN = {RFP_SHAPE_NONE,
                                {"NO CONNECTOR", "אין מחבר"},
                                {"THIS BOARD HAS NO PINS FOR A MODULE", "ללוח הזה אין פינים למודול"},
                                0,
                                0,
                                NULL,
                                -1,
                                "",
                                -1,
                                ""};
#define CONN_IR   CONN
#define CONN_UART CONN
static const rfp_chip_t CHIPS0[] = {
        {0x34, "AXP2101 (THE POWER CHIP)"},
        {0x20, "TCA9554 (THE IO EXPANDER)"},
        {0x38, "FT3168 (THE TOUCH)"},
        {0x6B, "QMI8658 (THE IMU)"},
        {0x18, "ES8311 (THE SPEAKER CODEC)"},
        {0x51, "PCF85063 (THE CLOCK)"},
        {0, NULL},
};
static const rfp_i2c_t I2C[] = {
        {{"THE INTERNAL BUS", "האפיק הפנימי"},
         {RFP_SHAPE_NONE,
          {"THE TEST PADS TP8 AND TP7", "פדי הבדיקה TP8 ו-TP7"},
          {"SDA GPIO15, SCL GPIO14", "SDA GPIO15, SCL GPIO14"},
          0,
          0,
          NULL,
          15,
          "TP8",
          14,
          "TP7"},
         0,
         true,
         CHIPS0},
};
