// RFPULSE: the wiring data of the board Waveshare ESP32-S3-Touch-LCD-3.49:
// the pads, the sockets and the I2C buses that the radio and bus apps show. libs/rfpulse/rfp_board.c includes
// this file (BOARD_RFP_WIRING of board_caps.h); the facts come from docs/headers.md.
// The 3.49: the two rows of pads on the BACK of the board, the battery end at the left and the USB-C port at the right
// (the silkscreen and the Waveshare picture). GND = the G pad of the BAT row: on a 3.49 board the G pad of the 3V3 row gave
// no ground to the module
static const rfp_pad_t PADS[] = {
        {"BAT", O}, {"3V3", O}, {"G", G},  {"SCL", O}, {"SDA", O}, {"44", O}, {"43", O}, {"41", O},
        {"40", O},  {"39", O},  {"38", O}, {"5V", O},  {"3V3", V}, {"G", O},  {"20", O}, {"19", O},
        {"5", D},   {"4", O},   {"3", O},  {"2", O},   {"1", O},   {"0", O},
};
static const rfp_conn_t CONN = {
        RFP_SHAPE_349,
        {"THE PADS OF THE 3.49", "הפדים של ה-3.49"},
        {"ON THE BACK OF THE BOARD, THE USB-C PORT AT THE RIGHT", "בגב הלוח, יציאת ה-USB-C מימין"},
        2,
        11,
        PADS,
        5,
        "5",
        -1,
        ""};
// the infrared modules: DATA pad 2, TX pad 1 (GPIO3 is a strapping pin: not used); the RF receiver can stay on pad 5
static const rfp_pad_t PADS_IR[] = {
        {"BAT", O}, {"3V3", O}, {"G", G},  {"SCL", O}, {"SDA", O}, {"44", O}, {"43", O}, {"41", O},
        {"40", O},  {"39", O},  {"38", O}, {"5V", O},  {"3V3", V}, {"G", O},  {"20", O}, {"19", O},
        {"5", O},   {"4", O},   {"3", O},  {"2", D},   {"1", T},   {"0", O},
};
static const rfp_conn_t CONN_IR = {
        RFP_SHAPE_349,
        {"THE PADS OF THE 3.49", "הפדים של ה-3.49"},
        {"ON THE BACK OF THE BOARD, THE USB-C PORT AT THE RIGHT", "בגב הלוח, יציאת ה-USB-C מימין"},
        2,
        11,
        PADS_IR,
        2,
        "2",
        1,
        "1"};
// a serial device: RX pad 44 (U0RXD), TX pad 43 (U0TXD), G of the top row
static const rfp_pad_t PADS_UART[] = {
        {"BAT", O}, {"3V3", O}, {"G", G},  {"SCL", O}, {"SDA", O}, {"44", R}, {"43", T}, {"41", O},
        {"40", O},  {"39", O},  {"38", O}, {"5V", O},  {"3V3", O}, {"G", O},  {"20", O}, {"19", O},
        {"5", O},   {"4", O},   {"3", O},  {"2", O},   {"1", O},   {"0", O},
};
static const rfp_conn_t CONN_UART = {
        RFP_SHAPE_349,
        {"THE PADS OF THE 3.49", "הפדים של ה-3.49"},
        {"ON THE BACK OF THE BOARD, THE USB-C PORT AT THE RIGHT", "בגב הלוח, יציאת ה-USB-C מימין"},
        2,
        11,
        PADS_UART,
        44,
        "44",
        43,
        "43"};
// the I2C bus of the system: SCL / SDA pads (GPIO48 / GPIO47)
static const rfp_pad_t PADS_I2C0[] = {
        {"BAT", O}, {"3V3", O}, {"G", G},  {"SCL", SC}, {"SDA", SD}, {"44", O}, {"43", O}, {"41", O},
        {"40", O},  {"39", O},  {"38", O}, {"5V", O},   {"3V3", V},  {"G", O},  {"20", O}, {"19", O},
        {"5", O},   {"4", O},   {"3", O},  {"2", O},    {"1", O},    {"0", O},
};
static const rfp_chip_t CHIPS0[] = {
        {0x20, "TCA9554 (THE IO EXPANDER)"},  {0x6B, "QMI8658 (THE IMU)"},        {0x51, "PCF85063 (THE CLOCK)"},
        {0x18, "ES8311 (THE SPEAKER CODEC)"}, {0x40, "ES7210 (THE MICROPHONES)"}, {0, NULL},
};
static const rfp_i2c_t I2C[] = {
        {{"THE BUS OF THE BOARD", "האפיק של הלוח"},
         {RFP_SHAPE_349,
          {"THE PADS OF THE 3.49", "הפדים של ה-3.49"},
          {"ON THE BACK OF THE BOARD, THE USB-C PORT AT THE RIGHT", "בגב הלוח, יציאת ה-USB-C מימין"},
          2,
          11,
          PADS_I2C0,
          47,
          "SDA",
          48,
          "SCL"},
         0,
         true,
         CHIPS0},
};
