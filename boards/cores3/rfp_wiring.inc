// RFPULSE: the wiring data of the board M5Stack CoreS3-SE:
// the pads, the sockets and the I2C buses that the radio and bus apps show. libs/rfpulse/rfp_board.c includes
// this file (BOARD_RFP_WIRING of board_caps.h); the facts come from docs/headers.md.
// The M5-Bus (2 x 15) as it looks on the board: the BACK of the CoreS3, the USB-C at the right.
// Then the socket has two COLUMNS: LEFT = the odd pins, RIGHT = the even pins, pin 1 (GND) at the top left (as the
// M5Stack diagram). The list is row by row: (1, 2), (3, 4) ...; the app puts the odd pin of each row at the left
static const rfp_pad_t PADS[] = {
        // (the names of the M5Stack picture of the bus)
        {"GND", G}, {"G10", O}, {"GND", O}, {"G8", O},  {"GND", O}, {"EN", O},  {"G37", O}, {"G5", D},
        {"G35", O}, {"G9", O},  {"G36", O}, {"3V3", V}, {"G44", O}, {"G43", O}, {"G18", O}, {"G17", O},
        {"G12", O}, {"G11", O}, {"G2", O},  {"G1", O},  {"G6", O},  {"G7", O},  {"G13", O}, {"G0", O},
        {"NC", O},  {"G14", O}, {"NC", O},  {"5V", O},  {"NC", O},  {"BAT", O},
};
static const rfp_conn_t CONN = {RFP_SHAPE_BUS30,
                                {"THE M5-BUS SOCKET", "שקע M5-BUS"},
                                {"BACK, USB-C AT RIGHT", "גב, USB-C מימין"},
                                15,
                                2,
                                PADS,
                                5,
                                "G5",
                                -1,
                                ""};
// the infrared modules: DATA G7 (pin 22), TX G10 (pin 2)
static const rfp_pad_t PADS_IR[] = {
        {"GND", G}, {"G10", T}, {"GND", O}, {"G8", O},  {"GND", O}, {"EN", O},  {"G37", O}, {"G5", O},
        {"G35", O}, {"G9", O},  {"G36", O}, {"3V3", V}, {"G44", O}, {"G43", O}, {"G18", O}, {"G17", O},
        {"G12", O}, {"G11", O}, {"G2", O},  {"G1", O},  {"G6", O},  {"G7", D},  {"G13", O}, {"G0", O},
        {"NC", O},  {"G14", O}, {"NC", O},  {"5V", O},  {"NC", O},  {"BAT", O},
};
static const rfp_conn_t CONN_IR = {RFP_SHAPE_BUS30,
                                   {"THE M5-BUS SOCKET", "שקע M5-BUS"},
                                   {"BACK, USB-C AT RIGHT", "גב, USB-C מימין"},
                                   15,
                                   2,
                                   PADS_IR,
                                   7,
                                   "G7",
                                   10,
                                   "G10"};
// a serial device: PORT.C (RX G18, TX G17), the same pins as M5-Bus pins 15 and 16
static const rfp_pad_t PADS_UART[] = {
        {"GND", G}, {"G10", O}, {"GND", O}, {"G8", O},  {"GND", O}, {"EN", O},  {"G37", O}, {"G5", O},
        {"G35", O}, {"G9", O},  {"G36", O}, {"3V3", O}, {"G44", O}, {"G43", O}, {"G18", R}, {"G17", T},
        {"G12", O}, {"G11", O}, {"G2", O},  {"G1", O},  {"G6", O},  {"G7", O},  {"G13", O}, {"G0", O},
        {"NC", O},  {"G14", O}, {"NC", O},  {"5V", O},  {"NC", O},  {"BAT", O},
};
static const rfp_conn_t CONN_UART = {RFP_SHAPE_BUS30,
                                     {"PORT.C OR THE M5-BUS", "PORT.C או M5-BUS"},
                                     {"BACK, USB-C AT RIGHT", "גב, USB-C מימין"},
                                     15,
                                     2,
                                     PADS_UART,
                                     18,
                                     "G18",
                                     17,
                                     "G17"};
static const rfp_pad_t PADS_I2C0[] = {
        {"GND", G},  {"G10", O},  {"GND", O}, {"G8", O},  {"GND", O}, {"EN", O},  {"G37", O}, {"G5", O},
        {"G35", O},  {"G9", O},   {"G36", O}, {"3V3", V}, {"G44", O}, {"G43", O}, {"G18", O}, {"G17", O},
        {"G12", SD}, {"G11", SC}, {"G2", O},  {"G1", O},  {"G6", O},  {"G7", O},  {"G13", O}, {"G0", O},
        {"NC", O},   {"G14", O},  {"NC", O},  {"5V", O},  {"NC", O},  {"BAT", O},
};
static const rfp_pad_t PADS_I2C1[] = {
        {"GND", G}, {"G10", O}, {"GND", O}, {"G8", O},  {"GND", O}, {"EN", O},  {"G37", O}, {"G5", O},
        {"G35", O}, {"G9", O},  {"G36", O}, {"3V3", V}, {"G44", O}, {"G43", O}, {"G18", O}, {"G17", O},
        {"G12", O}, {"G11", O}, {"G2", SD}, {"G1", SC}, {"G6", O},  {"G7", O},  {"G13", O}, {"G0", O},
        {"NC", O},  {"G14", O}, {"NC", O},  {"5V", O},  {"NC", O},  {"BAT", O},
};
static const rfp_chip_t CHIPS0[] = {
        {0x34, "AXP2101 (THE POWER CHIP)"},
        {0x58, "AW9523B (THE IO EXPANDER)"},
        {0x38, "FT6336U (THE TOUCH)"},
        {0x36, "AW88298 (THE SPEAKER AMPLIFIER)"},
        {0x40, "ES7210 (THE MICROPHONES)"},
        {0x51, "BM8563 (THE CLOCK)"},
        {0x69, "BMI270 (THE IMU)"},
        {0x21, "GC0308 (THE CAMERA)"},
        {0x23, "LTR-553ALS (THE LIGHT SENSOR)"},
        {0, NULL},
};
static const rfp_chip_t CHIPS_NONE[] = {{0, NULL}};
static const rfp_i2c_t I2C[] = {
        {{"THE INTERNAL BUS", "האפיק הפנימי"},
         {RFP_SHAPE_BUS30,
          {"THE M5-BUS SOCKET", "שקע M5-BUS"},
          {"BACK, USB-C AT RIGHT", "גב, USB-C מימין"},
          15,
          2,
          PADS_I2C0,
          12,
          "G12",
          11,
          "G11"},
         0,
         true,
         CHIPS0},
        {{"PORT.A", "PORT.A"},
         {RFP_SHAPE_BUS30,
          {"PORT.A OR THE M5-BUS", "PORT.A או M5-BUS"},
          {"BACK, USB-C AT RIGHT", "גב, USB-C מימין"},
          15,
          2,
          PADS_I2C1,
          2,
          "G2",
          1,
          "G1"},
         1,
         false,
         CHIPS_NONE},
};
