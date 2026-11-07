// RFPULSE: the wiring data of the board M5Stack Tab5:
// the pads, the sockets and the I2C buses that the radio and bus apps show. libs/rfpulse/rfp_board.c includes
// this file (BOARD_RFP_WIRING of board_caps.h); the facts come from docs/headers.md.
// ExtPort1 (J9, 2 x 5): the odd pins in one row, the even pins in the other
static const rfp_pad_t PADS[] = {
        {"1", O}, {"3", G}, {"5", O}, {"7", O}, {"9", O}, {"2", O}, {"4", O}, {"6", V}, {"8", D}, {"10", O},
};
static const rfp_conn_t CONN = {RFP_SHAPE_J9,
                                {"EXTPORT1 (J9)", "EXTPORT1 (J9)"},
                                {"ON THE BACK OF THE TAB5", "בגב ה-TAB5"},
                                2,
                                5,
                                PADS,
                                1,
                                "PIN 8",
                                -1,
                                ""};
// the infrared modules: DATA pin 9 (GPIO49), TX pin 10 (GPIO50)
static const rfp_pad_t PADS_IR[] = {
        {"1", O}, {"3", G}, {"5", O}, {"7", O}, {"9", D}, {"2", O}, {"4", O}, {"6", V}, {"8", O}, {"10", T},
};
static const rfp_conn_t CONN_IR = {RFP_SHAPE_J9,
                                   {"EXTPORT1 (J9)", "EXTPORT1 (J9)"},
                                   {"ON THE BACK OF THE TAB5", "בגב ה-TAB5"},
                                   2,
                                   5,
                                   PADS_IR,
                                   49,
                                   "PIN 9",
                                   50,
                                   "PIN 10"};
// a serial device: RX pin 9 (GPIO49), TX pin 10 (GPIO50)
static const rfp_pad_t PADS_UART[] = {
        {"1", O}, {"3", G}, {"5", O}, {"7", O}, {"9", R}, {"2", O}, {"4", O}, {"6", O}, {"8", O}, {"10", T},
};
static const rfp_conn_t CONN_UART = {RFP_SHAPE_J9,
                                     {"EXTPORT1 (J9)", "EXTPORT1 (J9)"},
                                     {"ON THE BACK OF THE TAB5", "בגב ה-TAB5"},
                                     2,
                                     5,
                                     PADS_UART,
                                     49,
                                     "PIN 9",
                                     50,
                                     "PIN 10"};
static const rfp_chip_t CHIPS0[] = {
        {0x43, "PI4IOE5V6408 (AN IO EXPANDER)"},
        {0x44, "PI4IOE5V6408 (AN IO EXPANDER)"},
        {0x14, "GT911 (THE TOUCH)"},
        {0x55, "THE TOUCH (ST7123 PANEL)"},
        {0x10, "ES8388 (THE SPEAKER CODEC)"},
        {0x40, "ES7210 (THE MICROPHONES)"},
        {0x68, "BMI270 (THE IMU)"},
        {0x41, "INA226 (THE BATTERY MONITOR)"},
        {0x32, "RX8130CE (THE CLOCK)"},
        {0, NULL},
};
static const rfp_i2c_t I2C[] = {
        {{"THE INTERNAL BUS", "האפיק הפנימי"},
         {RFP_SHAPE_NONE,
          {"EXTPORT2, PINS 5 AND 6", "EXTPORT2, פינים 5 ו-6"},
          {"SDA GPIO31, SCL GPIO32", "SDA GPIO31, SCL GPIO32"},
          0,
          0,
          NULL,
          31,
          "GPIO31",
          32,
          "GPIO32"},
         0,
         true,
         CHIPS0},
};
