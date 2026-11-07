// RFPULSE: the wiring data of the board of the build (rfp_board.h; boards/<kind>/rfp_wiring.inc).
#include "rfp_board.h"

#include <stddef.h>
#include "tfn/board.h"  // (BOARD_RFP_WIRING)

#define O  RFP_PAD_OTHER
#define V  RFP_PAD_3V3
#define G  RFP_PAD_GND
#define D  RFP_PAD_DATA
#define T  RFP_PAD_TX
#define R  RFP_PAD_RX
#define SD RFP_PAD_SDA
#define SC RFP_PAD_SCL

#include BOARD_RFP_WIRING  // (boards/<kind>/rfp_wiring.inc: the wiring data of the board of the build)

const rfp_conn_t *rfp_board_conn(void)
{
    return &CONN;
}
const rfp_conn_t *rfp_board_ir(void)
{
    return &CONN_IR;
}
const rfp_conn_t *rfp_board_uart(void)
{
    return &CONN_UART;
}
const rfp_i2c_t *rfp_board_i2c(int i)
{
    return i >= 0 && i < (int)(sizeof I2C / sizeof I2C[0]) ? &I2C[i] : NULL;
}
