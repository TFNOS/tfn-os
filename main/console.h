#pragma once

// Commands on the USB serial port (one line each):
//   wifi <name> <password>   connect and save the network
//   wifi forget              forget the saved network
//   ip                       show the WiFi state and address
void console_init(void);
