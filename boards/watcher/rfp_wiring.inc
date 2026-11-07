// RFPULSE: the wiring data of the board Seeed SenseCAP Watcher:
// the pads, the sockets and the I2C buses that the radio and bus apps show. libs/rfpulse/rfp_board.c includes
// this file (BOARD_RFP_WIRING of board_caps.h); the facts come from docs/headers.md.
// J2 (2 x 4): pins 1 to 4 in one row, 5 to 8 in the other (pin 1 across from pin 8)
static const rfp_pad_t PADS[] = {
        {"1", G}, {"2", V}, {"3", O}, {"4", D}, {"8", O}, {"7", O}, {"6", O}, {"5", O},
};
static const rfp_conn_t CONN = {RFP_SHAPE_J2,
                                {"THE J2 SOCKET", "שקע J2"},
                                {"ON THE BACK OF THE WATCHER", "בגב ה-WATCHER"},
                                2,
                                4,
                                PADS,
                                19,
                                "PIN 4",
                                -1,
                                ""};
// the infrared modules: DATA pin 4 (GPIO19, also the RF receiver: J2 has only two free pins), TX pin 5 (GPIO20)
static const rfp_pad_t PADS_IR[] = {
        {"1", G}, {"2", V}, {"3", O}, {"4", D}, {"8", O}, {"7", O}, {"6", O}, {"5", T},
};
static const rfp_conn_t CONN_IR = {RFP_SHAPE_J2,
                                   {"THE J2 SOCKET", "שקע J2"},
                                   {"ON THE BACK OF THE WATCHER", "בגב ה-WATCHER"},
                                   2,
                                   4,
                                   PADS_IR,
                                   19,
                                   "PIN 4",
                                   20,
                                   "PIN 5"};
// a serial device: RX pin 4 (GPIO19), TX pin 5 (GPIO20)
static const rfp_pad_t PADS_UART[] = {
        {"1", G}, {"2", O}, {"3", O}, {"4", R}, {"8", O}, {"7", O}, {"6", O}, {"5", T},
};
static const rfp_conn_t CONN_UART = {RFP_SHAPE_J2,
                                     {"THE J2 SOCKET", "שקע J2"},
                                     {"ON THE BACK OF THE WATCHER", "בגב ה-WATCHER"},
                                     2,
                                     4,
                                     PADS_UART,
                                     19,
                                     "PIN 4",
                                     20,
                                     "PIN 5"};
// I2C0: SDA pin 3 (GPIO47), SCL pin 6 (GPIO48)
static const rfp_pad_t PADS_I2C0[] = {
        {"1", G}, {"2", V}, {"3", SD}, {"4", O}, {"8", O}, {"7", O}, {"6", SC}, {"5", O},
};
static const rfp_chip_t CHIPS0[] = {
        {0x21, "PCA9535 (THE IO EXPANDER)"}, {0x51, "PCF8563 (THE CLOCK)"},      {0x18, "ES8311 (THE SPEAKER CODEC)"},
        {0x13, "ES7243 (THE MICROPHONE)"},   {0x14, "ES7243E (THE MICROPHONE)"}, {0, NULL},
};
static const rfp_i2c_t I2C[] = {
        {{"I2C0 (J2 AND GROVE)", "I2C0 (J2 ו-GROVE)"},
         {RFP_SHAPE_J2,
          {"THE J2 SOCKET", "שקע J2"},
          {"ON THE BACK OF THE WATCHER", "בגב ה-WATCHER"},
          2,
          4,
          PADS_I2C0,
          47,
          "PIN 3",
          48,
          "PIN 6"},
         0,
         true,
         CHIPS0},
};
