// RFPULSE: the wiring data of the board Elecrow CrowPanel Advance 7.0" HMI:
// the pads, the sockets and the I2C buses that the radio and bus apps show. libs/rfpulse/rfp_board.c includes
// this file (BOARD_RFP_WIRING of board_caps.h); the facts come from docs/headers.md.
// the CrowPanel Advance 7.0: internal I2C bus (SDA GPIO15, SCL GPIO16) with STC8H1K28 (0x30) and GT911 (0x5D)
static const rfp_conn_t CONN = {RFP_SHAPE_NONE,
                                {"UART1 / GPIO HEADER", "מחבר GPIO"},
                                {"ON THE BACK OF THE CROWPANEL 7.0", "בגב הלוח"},
                                0,
                                0,
                                NULL,
                                8,
                                "GPIO8",
                                -1,
                                ""};
#define CONN_IR   CONN
#define CONN_UART CONN
static const rfp_chip_t CHIPS0[] = {
        {0x30, "STC8H1K28 (BACKLIGHT & PERIPHERAL MCU)"},
        {0x5D, "GT911 (THE TOUCH)"},
        {0x14, "GT911 (THE TOUCH ALT)"},
        {0, NULL},
};
static const rfp_i2c_t I2C[] = {
        {{"THE INTERNAL BUS", "האפיק הפנימי"},
         {RFP_SHAPE_NONE,
          {"I2C HEADER", "מחבר I2C"},
          {"SDA GPIO15, SCL GPIO16", "SDA GPIO15, SCL GPIO16"},
          0,
          0,
          NULL,
          15,
          "GPIO15",
          16,
          "GPIO16"},
         0,
         true,
         CHIPS0},
};
