#pragma once
// RFPULSE: THE WIRING OF A RECEIVER MODULE ON EACH BOARD KIND (from docs/headers.md: free pins only). The data of one
// connector: its pads in rows, the role of each pad (3.3 V, GND, DATA, other), and how the app must draw it (the shape:
// the landmarks that show the user how the connector lies). The app draws; this file only knows the facts.
//   3.49     the 2 rows of 11 pads on the BACK of the board (the USB-C port at the right): 3V3, G and GPIO5 (bottom row)
//   CoreS3   the M5-Bus socket on the back: pin 12 (3V3), pin 1 (GND), pin 8 (GPIO5)
//   Watcher  the 2 x 4 socket J2 on the back: pin 2 (3V3), pin 1 (GND), pin 4 (GPIO19)
//   Tab5     ExtPort1 (J9, 2 x 5): pin 6 (3.3 V), pin 3 (GND), pin 8 (GPIO1)
//   AMOLED   no connector (no receiver)
// THE INFRARED MODULES (rfp_board_ir(), the IR DECODER app): the same connector, other pins: the DATA of an IR receiver
// module and the TX of an IR LED module (the RF receiver can stay on its pin)
//   3.49     3V3 (bottom row), G (top row), 2 (DATA), 1 (TX)
//   CoreS3   the M5-Bus: 3V3 (pin 12), GND (pin 1), G7 (pin 22, DATA), G10 (pin 2, TX)
//   Watcher  J2: pin 2 (3V3), pin 1 (GND), pin 4 (GPIO19, DATA: the pin of the RF receiver too), pin 5 (GPIO20, TX)
//   Tab5     J9: pin 6 (3.3 V), pin 3 (GND), pin 9 (GPIO49, DATA), pin 10 (GPIO50, TX)
// A SERIAL DEVICE (rfp_board_uart(), the SERIAL MONITOR app): RX (the board reads: the TX of the device), TX (the board
// sends: the RX of the device), GND. 3.3 V levels only.
//   3.49     the pads 44 (RX, U0RXD) and 43 (TX, U0TXD: the ROM sends its boot text on it), G (top row)
//   CoreS3   PORT.C, also on the M5-Bus: G18 (pin 15, RX), G17 (pin 16, TX), GND (pin 1)
//   Watcher  J2: pin 4 (GPIO19, RX), pin 5 (GPIO20, TX), pin 1 (GND)
//   Tab5     J9: pin 9 (GPIO49, RX), pin 10 (GPIO50, TX), pin 3 (GND)
// THE I2C BUSES (rfp_board_i2c(i), the I2C SCANNER app): the bus of the system (its chips work on it all the time: the
// app takes its handle) and a free bus where the board has one (the app makes it and deletes it).
//   3.49     the pads SCL / SDA (GPIO48 / GPIO47), 3V3 (bottom row), G (top row)
//   CoreS3   the internal bus (G12 / G11, M5-Bus pins 17 / 18) and PORT.A (G2 / G1, M5-Bus pins 19 / 20)
//   Watcher  I2C0 (J2: pin 3 SDA GPIO47, pin 6 SCL GPIO48)
//   Tab5     the internal bus (GPIO31 / GPIO32: ExtPort2 pins 5 / 6)
//   AMOLED   the internal bus (GPIO15 / GPIO14: the test pads TP8 / TP7)
#include <stdbool.h>
#include <stdint.h>

enum {
    RFP_PAD_OTHER,
    RFP_PAD_3V3,
    RFP_PAD_GND,
    RFP_PAD_DATA,
    RFP_PAD_TX,
    RFP_PAD_RX,
    RFP_PAD_SDA,
    RFP_PAD_SCL
};
enum {
    RFP_SHAPE_NONE,
    RFP_SHAPE_349,
    RFP_SHAPE_BUS30,
    RFP_SHAPE_J2,
    RFP_SHAPE_J9
};

typedef struct {
    const char *label;  // the text on the board or in the schematic ("3V3", "G", "5", "12")
    uint8_t role;       // RFP_PAD_*
} rfp_pad_t;

typedef struct {
    uint8_t shape;         // RFP_SHAPE_*
    const char *name[2];   // the name of the connector: English, Hebrew
    const char *where[2];  // where it is: English, Hebrew
    uint8_t rows, cols;    // the pads: rows * cols, row by row, as the user sees them in the sketch
    const rfp_pad_t *pads;
    int data_gpio;         // the GPIO of DATA (-1: no connector)
    const char *data_pin;  // the name of the DATA pad for the text ("5", "PIN 8")
    int tx_gpio;           // the GPIO of TX (an IR LED; -1: none)
    const char *tx_pin;
} rfp_conn_t;

// The connector of this board kind
const rfp_conn_t *rfp_board_conn(void);
// The connector of this board kind with the pins of the infrared modules (RFP_PAD_DATA = the IR receiver, RFP_PAD_TX =
// the IR LED)
const rfp_conn_t *rfp_board_ir(void);
// The connector with the pins of a serial device: RFP_PAD_RX (data_gpio), RFP_PAD_TX (tx_gpio), RFP_PAD_GND
const rfp_conn_t *rfp_board_uart(void);

typedef struct {
    uint8_t addr;      // 7 bits
    const char *name;  // "QMI8658 (THE IMU)"
} rfp_chip_t;
typedef struct {
    const char *name[2];      // "THE INTERNAL BUS", "PORT.A": English, Hebrew
    rfp_conn_t conn;          // the connector: RFP_PAD_SDA (data_gpio), RFP_PAD_SCL (tx_gpio), 3V3, GND
    int8_t port;              // the I2C port
    bool shared;              // true: the bus of the system (i2c_master_get_bus_handle(port)); false: a free bus
    const rfp_chip_t *chips;  // the chips of the board on this bus (the list ends with addr 0)
} rfp_i2c_t;
// The I2C bus i of this board kind (0 = the bus of the system); NULL after the last one
const rfp_i2c_t *rfp_board_i2c(int i);
