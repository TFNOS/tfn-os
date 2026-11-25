# The extension connectors of the boards

This file gives the connectors for external modules on each board kind: each pin, its GPIO, and the use of that pin
in our firmware. It tells which pins are free, and the cautions.

| Board | The connectors for modules | Section |
|---|---|---|
| Waveshare ESP32-S3-Touch-LCD-3.49 | 2 rows of 11 through-hole pads (2.54 mm) | [1](#1-the-waveshare-349) |
| M5Stack CoreS3 | The M5-Bus socket (2 x 15, 2.54 mm) on the back; PORT.A on the core; PORT.B and PORT.C on the DIN Base | [2](#2-the-m5stack-cores3) |
| Seeed SenseCAP Watcher | A 2 x 4 socket (2.54 mm) on the back; a Grove I2C port | [3](#3-the-seeed-sensecap-watcher) |
| M5Stack Tab5 (ESP32-P4) | The M5-Bus socket (2 x 15) on the back; ExtPort1 (2 x 5 header, 2.54 mm); ExtPort2 (RS-485 + I2C); PORT.A; the STAMP pads | [4](#4-the-m5stack-tab5) |
| Waveshare ESP32-S3-Touch-AMOLED-1.8 | No pin header and no socket. Only small test pads on the PCB | [5](#5-the-waveshare-amoled-18) |
| Elecrow CrowPanel Advance 7.0 | The GPIO8 UART1/GPIO header and the I2C header (GPIO15, GPIO16) on the back | [6](#6-the-elecrow-crowpanel-advance-70) |

The sources:
- 3.49: the V1 schematic (https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-3.49/ESP32-S3-Touch-LCD-3.49-Schematic.pdf,
  connectors J5 and J6), the V2 schematic (https://github.com/waveshareteam/ESP32-S3-Touch-LCD-3.49-V2, `schematic/`,
  connectors J3 and J6), the PCB picture
  (https://www.waveshare.com/img/devkit/ESP32-S3-Touch-LCD-3.49/ESP32-S3-Touch-LCD-3.49-details-intro.jpg).
- CoreS3: the M5Stack page https://docs.m5stack.com/en/core/CoreS3 (the M5-Bus, the ports, the camera).
- Watcher: the schematic `Hardware/SenseCAP_Watcher_v1.0_SCH.pdf` of https://github.com/Seeed-Studio/OSHW-SenseCAP-Watcher
  (sheet 10 "Port & IO Exp & RTC": J2 and J5), and the BSP header
  `components/sensecap-watcher/include/sensecap-watcher.h` of https://github.com/Seeed-Studio/SenseCAP-Watcher-Firmware.
- Tab5: the schematic https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1132/Tab5_Schematics_PDF.pdf (page 4: BUS1,
  J9, J7, J11, the STAMP pads), the pin map picture
  https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1132/C145_Pinmap_Overview.png, the page
  https://docs.m5stack.com/en/core/Tab5.
- AMOLED: the schematic https://files.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.8/ESP32-S3-Touch-AMOLED-1.8.pdf
  (TP2 to TP18).
- Our pin maps: `main/board.c`, `main/board_cores3.c`, `main/board_watcher.c`, `main/board_tab5.c`,
  `main/board_amoled.c`, `main/audio.c`, `main/webcam.c`, `sdkconfig.defaults.tab5`.

The rules for all boards:
- All the GPIOs use 3.3 V logic. Do not connect a 5 V signal to a GPIO pin.
- An external I2C module on a shared bus must use an address that is not in the table of that board. Its driver must
  use the same bus handle as the board file, not a second driver on the same pins.
- "Free" in this file means: no part of the board and no code of our firmware uses the pin now. A new use must go into
  this file.

---

## 1. The Waveshare 3.49

22 through-hole pads (2.54 mm pitch): two rows of 11 pads. V1 and V2 have the same signals on the same pads. Only the
pin numbers in the two schematics are in the opposite order. The V1/V2 pin changes (MANUAL.md "Board revisions") do
not touch the header.

The rows are in the order of the PCB silkscreen, from the battery end to the USB-C end.

The row at the top of the PCB picture (V2: J3, V1: J5):

| Silkscreen | Schematic net | ESP32-S3 pin | Use in our firmware | Free |
|---|---|---|---|---|
| BAT | VBAT | - | The battery + line | Power |
| 3V3 | 3V3 | - | The 3.3 V rail (MP1605, 2 A for the full board) | Power |
| G | GND | - | Ground | Power |
| SCL | IO48 | GPIO48 | The internal I2C bus (I2C_NUM_0, `PIN_I2C0_SCL`); I2C SCANNER scans it | Shared |
| SDA | IO47 | GPIO47 | The internal I2C bus (I2C_NUM_0, `PIN_I2C0_SDA`); I2C SCANNER scans it | Shared |
| 44 | IO44 / RXD | GPIO44 (U0RXD) | SERIAL MONITOR: RX (UART1, while the app is open) | App |
| 43 | IO43 / TXD | GPIO43 (U0TXD) | SERIAL MONITOR: TX (UART1, while the app is open) | App (see the cautions) |
| 41 | SD_SCLK | GPIO41 | The TF card CLK (`PIN_SD_CLK`) | No |
| 40 | SD_MISO | GPIO40 | The TF card D0 (`PIN_SD_D0`) | No |
| 39 | SD_MOSI | GPIO39 | The TF card CMD (`PIN_SD_CMD`) | No |
| 38 | SD_CS | GPIO38 | The TF card CD/D3, with a 10K pull-up to 3V3 | No (see the cautions) |

The row at the bottom of the PCB picture (J6 on V1 and V2):

| Silkscreen | Schematic net | ESP32-S3 pin | Use in our firmware | Free |
|---|---|---|---|---|
| 5V | VBUS | - | The USB-C 5 V line | Power |
| 3V3 | 3V3 | - | The 3.3 V rail | Power |
| G | GND | - | Ground (on one tested board this pad gave a receiver module no ground: use the G pad of the top row) | Power |
| 20 | U_P | GPIO20 (USB D+) | The native USB: console, flash, USB audio and MIDI | No |
| 19 | U_N | GPIO19 (USB D-) | The native USB | No |
| 5 | IO5 | GPIO5 | RF DECODER: the DATA of a receiver module (while the app is open) | App |
| 4 | IO4 / BAT_ADC | GPIO4 (ADC1 channel 3) | The battery voltage (`board_battery()`); VBAT through 200K/100K | No |
| 3 | IO3 | GPIO3 | Not used | Yes (strapping pin) |
| 2 | IO2 | GPIO2 | IR DECODER: the DATA of an IR receiver module (while the app is open) | App |
| 1 | IO1 | GPIO1 | IR DECODER: the TX of an IR LED module (a send; then LOW) | App |
| 0 | IO0 / BOOT0 | GPIO0 | The BOOT key (`PIN_BOOT_KEY`) | No |

What is free:
- GPIO1, GPIO2, GPIO5: free. No other part of the board connects to them. RF DECODER uses GPIO5 (the DATA of a
  receiver module) while it is open; IR DECODER uses GPIO2 (the DATA of an IR receiver, the pull-up) and GPIO1 (the
  TX of an IR LED: the RMT) (`libs/rfpulse/rfp_board.c`: the wiring of each board).
- GPIO3: free, but it is a strapping pin (the JTAG source). Do not pull it high or low at boot.
- GPIO43, GPIO44: free. They are the UART0 pins. The board has no USB-UART chip: the USB-C port goes directly to
  GPIO19/GPIO20. SERIAL MONITOR uses them (UART1: RX GPIO44 with the pull-up, TX GPIO43) while it is open; at its
  close they go back to inputs.
- The I2C bus on SCL/SDA (GPIO48/GPIO47), with the address limits below.
- The power pads: 5V, 3V3, BAT, GND.
- GPIO1, GPIO2 and GPIO3 are on ADC1 (channels 0, 1 and 2). GPIO5 is ADC1 channel 4. ADC1 works when WiFi is on.
- If an app does not use the TF card, GPIO39, GPIO40 and GPIO41 are free too. GPIO38 is free only when the card slot
  is empty (see the cautions).

The I2C bus on the header is the same bus as these chips (the V2 schematic table, our code):

| Chip | Address | Where |
|---|---|---|
| TCA9554 (the IO expander) | 0x20 | `main/board.c` `TCA_ADDR` |
| QMI8658 (the IMU) | 0x6B | `main/imu.c` `QMI_ADDR` |
| PCF85063 (the RTC) | 0x51 | The chip address |
| ES8311 (the speaker codec) | 0x18 | The chip address |
| ES7210 (the microphone ADC) | 0x40 | The chip address |

The touch controller (0x3B) is on a different bus (GPIO17/GPIO18), and that bus is not on the header. The firmware
tasks use the header bus all the time (the IMU, the expander, the audio codecs).

The cautions:
- 5V (VBUS) has power only when a USB cable is connected. BAT has the battery voltage (about 3.0 V to 4.2 V).
- GPIO38 connects to the CD/D3 pin of the TF card. In our 1-bit SD mode, D3 must stay high (the 10K pull-up does
  this). If a device pulls GPIO38 low, the card can go into SPI mode when it starts, and the SD card fails.
- GPIO43 (U0TXD): the ESP32-S3 ROM sends its boot text on this pin at each start. A device on GPIO43 must accept
  this text, or must ignore the line until the firmware starts.
- GPIO0 and GPIO3 are strapping pins. A device that pulls GPIO0 low at boot puts the board into the download mode.
- GPIO19 and GPIO20: if an app uses them as GPIOs, the USB port stops (no console, no USB flash) until a reset.
- GPIO45 and GPIO46 (the other strapping pins) are not on the header: they are I2S lines of the audio codecs.

---

## 2. The M5Stack CoreS3

The CoreS3 has three kinds of connectors:
- The M5-Bus: a 2 x 15 socket (2.54 mm) on the back of the core. The M5 modules plug into it.
- PORT.A (HY2.0-4P, red): on the core.
- PORT.B and PORT.C (HY2.0-4P): on the DIN Base (the bottom part of the CoreS3 kit), not on the core.

### The M5-Bus

The odd pins are in one row, the even pins in the other row (the M5Stack diagram). On the board (checked on a real
board): look at the BACK with the USB-C port at the right; then the socket is two columns, the odd pins at the
left, and pin 1 (GND) is at the top left.

| Pin | Signal | GPIO | Use in our firmware | Free | | Pin | Signal | GPIO | Use in our firmware | Free |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | GND | - | Ground | Power | | 2 | ADC | GPIO10 | IR DECODER: the TX of an IR LED module | App |
| 3 | GND | - | Ground | Power | | 4 | PB_IN | GPIO8 | OHMS: the ADC input (PORT.B) | App |
| 5 | GND | - | Ground | Power | | 6 | EN | RST | The reset line | No |
| 7 | MOSI | GPIO37 | The LCD and the SD card | Shared |  | 8 | GPIO | GPIO5 | RF DECODER: the DATA of a receiver module (while the app is open) | App |
| 9 | MISO | GPIO35 | The LCD D/C AND the SD card MISO | Shared | | 10 | PB_OUT | GPIO9 | OHMS: the drive output (PORT.B) | App |
| 11 | SCK | GPIO36 | The LCD and the SD card | Shared | | 12 | 3V3 | - | The 3.3 V rail | Power |
| 13 | RXD0 | GPIO44 | Not used | Yes | | 14 | TXD0 | GPIO43 | Not used | Yes (boot text) |
| 15 | PC_RX | GPIO18 | SERIAL MONITOR: RX (PORT.C, while the app is open) | App | | 16 | PC_TX | GPIO17 | SERIAL MONITOR: TX (PORT.C, while the app is open) | App |
| 17 | Int SDA | GPIO12 | The internal I2C bus (I2C SCANNER scans it) | Shared | | 18 | Int SCL | GPIO11 | The internal I2C bus (I2C SCANNER scans it) | Shared |
| 19 | PORT.A SDA | GPIO2 | I2C SCANNER: a bus of its own (I2C_NUM_1, while the app is open) | App | | 20 | PORT.A SCL | GPIO1 | I2C SCANNER: a bus of its own (I2C_NUM_1) | App |
| 21 | GPIO | GPIO6 | Not used | Yes | | 22 | GPIO | GPIO7 | IR DECODER: the DATA of an IR receiver module | App |
| 23 | I2S_DOUT | GPIO13 | The speaker data (AW88298) | No | | 24 | I2S_LRCK (M5 name) | GPIO0 | The I2S MCLK in `audio.c`; a strapping pin | No |
| 25 | HVIN | - | The DIN Base input voltage | Power | | 26 | I2S_DIN | GPIO14 | The microphone data (ES7210) | No |
| 27 | HVIN | - | The DIN Base input voltage | Power | | 28 | 5V | - | The 5 V bus (BUS_OUT) | Power |
| 29 | HVIN | - | The DIN Base input voltage | Power | | 30 | BAT | - | The battery + line | Power |

Notes on the M5-Bus:
- The pin names come from the Core2 (M5Stack keeps the same bus for all the Core models). The M5Stack page names pin
  24 I2S_LRCK. On the CoreS3 the I2S word clock is GPIO33 and the bit clock is GPIO34 (not on the bus). GPIO0 is the
  MCLK in our `audio.c`.
- GPIO35 is the D/C line of the LCD and the MISO line of the SD card (one line). An SPI module on pins 7, 9 and 11
  shares the bus with the LCD: it needs its own CS line (a free GPIO, for example GPIO5 or GPIO6) and it must use the
  same SPI bus handle (SPI3_HOST, `board_cores3.c`).

### PORT.A, PORT.B, PORT.C

The wire colours are in the order black, red, yellow, white.

| Port | Where | Black | Red | Yellow | White | Use in our firmware |
|---|---|---|---|---|---|---|
| PORT.A | The core | GND | 5V | GPIO2 (SDA) | GPIO1 (SCL) | Not used |
| PORT.B | The DIN Base | GND | 5V | GPIO9 | GPIO8 | OHMS (the resistance meter) |
| PORT.C | The DIN Base | GND | 5V | GPIO17 (TX) | GPIO18 (RX) | Not used |

The same GPIOs are on the M5-Bus. PORT.A is the external I2C bus of M5Stack (the Grove I2C units). It is a different
bus from the internal one: it has no chips of the board on it.

### What is free

- GPIO1, GPIO2 (PORT.A), GPIO17, GPIO18 (PORT.C), GPIO5, GPIO6, GPIO7, GPIO10 (ADC1 channel 9), GPIO43, GPIO44.
- GPIO8 and GPIO9 (PORT.B): free when OHMS is not open. GPIO8 is ADC1 channel 7.
- The internal I2C bus on M5-Bus pins 17/18, with the address limits below.

The internal I2C bus (GPIO12 SDA, GPIO11 SCL; `board_cores3.c`, the M5Stack page):

| Chip | Address |
|---|---|
| AXP2101 (the power chip) | 0x34 |
| AW9523B (the IO expander) | 0x58 |
| FT6336U (the touch) | 0x38 |
| AW88298 (the speaker amplifier) | 0x36 |
| ES7210 (the microphone ADC) | 0x40 |
| BM8563 (the RTC) | 0x51 |
| BMI270 (the IMU) | 0x69 |
| GC0308 (the camera) | 0x21 |
| LTR-553ALS (the light and proximity sensor) | 0x23 |

### The cautions

- The camera uses GPIO38 to GPIO42, GPIO15, GPIO16, GPIO45, GPIO46, GPIO47 and GPIO48 (not on the bus). GPIO3 (a
  strapping pin) is the LCD CS, and GPIO4 is the SD card CS. GPIO21 is the AW9523B interrupt.
- GPIO0 is a strapping pin. A device that pulls it low at boot puts the board into the download mode.
- GPIO43 (TXD0): the ROM sends its boot text on it at each start.
- The 5 V of the ports and of M5-Bus pin 28 is the BUS_OUT line. AW9523B P0_1 (BUS_OUT_EN) connects the boost (5 V
  from the battery) to BUS_OUT. Our firmware keeps the value of P0_1 that it reads at the start; only the USB
  headphones code sets it (`board_cores3.c`, `usb_phones.c`). This is not measured on our board: measure the 5 V
  before you connect a 5 V module.
- BAT has the battery voltage (about 3.0 V to 4.2 V). HVIN has power only from the DIN Base input.

---

## 3. The Seeed SenseCAP Watcher

The Watcher has two connectors for modules:
- J2: a 2 x 4 socket (2.54 mm) on the back ("Extension Interface": 1 x I2C, 2 x GPIO, 2 x GND, 3.3 V out, 5 V in).
- J5: the Grove I2C port (PHS-4P).

### J2, the 2 x 4 socket

| Pin | Net | ESP32-S3 pin | Use in our firmware | Free | | Pin | Net | ESP32-S3 pin | Use in our firmware | Free |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | GND | - | Ground | Power | | 8 | GND | - | Ground | Power |
| 2 | VCC_3V3 (through R139, 0R) | - | The 3.3 V rail, always on | Power | | 7 | Exp_5v (through F1, 2 A) | - | 5 V IN: a power input of the board | Power in |
| 3 | ESP_GPIO47/I2C0_SDA | GPIO47 | The I2C0 bus | Shared | | 6 | ESP_GPIO48/I2C0_SCL | GPIO48 | The I2C0 bus | Shared |
| 4 | ESP_GPIO19/USB_DM | GPIO19 | RF DECODER and IR DECODER: the DATA of a receiver module; SERIAL MONITOR: RX (while the app is open) | App | | 5 | ESP_GPIO20/USB_DP | GPIO20 | IR DECODER: the TX of an IR LED module; SERIAL MONITOR: TX | App |

The pin numbers are the numbers of the schematic. Pins 1 to 4 are one row and pins 5 to 8 are the other row (pin 1
across from pin 8). Find pin 1 on the PCB before you connect a module.

### J5, the Grove port

| Pin | Net | ESP32-S3 pin | Use in our firmware |
|---|---|---|---|
| 1 | SCL | GPIO48 | The I2C0 bus |
| 2 | SDA | GPIO47 | The I2C0 bus |
| 3 | GROVE_3.3V | - | 3.3 V through a switch (Q9) that PCA9535 P1.6 controls. **Our firmware keeps it OFF** |
| 4 | GND | - | Ground |

### What is free

- GPIO19 and GPIO20 (J2 pins 4 and 5). They are the native USB pins of the ESP32-S3, but on the Watcher the USB-C
  ports do not go to them: the console is UART0 through the USB-to-UART chip, and the Himax chip has the other port.
  The schematic also has a link from GPIO20 to the touch interrupt (R135), but R135 is not fitted.
  A module on J2 pins 4 and 5 can also be a USB device of the ESP32-S3 (D- on pin 4, D+ on pin 5).
- The I2C0 bus (J2 pins 3 and 6, the Grove port), with the address limits below.

The I2C0 bus (GPIO47 SDA, GPIO48 SCL; `board_watcher.c`, the Seeed BSP):

| Chip | Address |
|---|---|
| PCA9535 (the IO expander) | 0x21 |
| PCF8563 (the RTC) | 0x51 |
| ES8311 (the speaker codec) | 0x18 (the BSP gives the 8-bit form 0x30) |
| ES7243 / ES7243E (the microphone ADC) | 0x13 or 0x14 |

The schematic also connects the I2C slave pins of the Himax chip to this bus (R75, R76). Our firmware talks to the
Himax chip over SPI only. Its I2C address is not checked here: do an I2C scan before you add a module.
The touch controller (SPD2010, 0x53) is on a different bus (I2C1: GPIO39 SDA, GPIO38 SCL), and that bus is not on a
connector.

### The cautions

- J2 pin 7 is a 5 V INPUT, not an output. The PCA9535 reads it (Exp_5v_IN_DET). The Watcher gives no 5 V to a
  module. A module that needs 5 V must have its own supply.
- The Grove port has no power in our firmware: `P1_START` in `board_watcher.c` does not have `P1_GROVE`. A Grove
  module needs a change in `board_watcher.c` (turn on P1.6). The Seeed BSP turns it on at the start.
- The other ESP32-S3 pins of the Watcher are all in use (the screen, the SD card and the Himax SPI, the audio, the
  wheel, the RGB LED on GPIO40, the Himax UART on GPIO17/GPIO18).

---

## 4. The M5Stack Tab5

The Tab5 (ESP32-P4) has these connectors:
- The M5-Bus: a 2 x 15 socket on the back (the schematic: BUS1, "BUS same as CORE2 with old GPIO map").
- ExtPort1: J9, a 2 x 5 header (2.54 mm).
- ExtPort2: J7, a 1.25 mm 6-pin connector with RS-485 and the internal I2C bus.
- PORT.A: J11, Grove (HY2.0-4P).
- The STAMP pads: 12 pads for a STAMP module (Cat.M, NB-IoT, LoRaWAN). They are SMD pads, not a socket.

### The M5-Bus

The Core2 names are the names on the schematic symbol. The Tab5 GPIO is the net on that pin.

| Pin | Core2 name | Tab5 GPIO | Free | | Pin | Core2 name | Tab5 GPIO | Free |
|---|---|---|---|---|---|---|---|---|
| 1 | GND | - | Power | | 2 | G35/ADC | GPIO16 | Yes |
| 3 | GND | - | Power | | 4 | G36/ADC | GPIO17 | Yes |
| 5 | GND | - | Power | | 6 | EN/RST | SOC_RST (the reset) | No |
| 7 | G23/MOSI | GPIO18 (SPI MOSI) | Yes (STAMP) | | 8 | G25/DAC | GPIO45 | Yes |
| 9 | G38/MISO | GPIO19 (SPI MISO) | Yes (STAMP) | | 10 | G26/DAC | GPIO52 | Yes |
| 11 | G18/SCK | GPIO5 (SPI SCK) | Yes (STAMP) | | 12 | 3.3V | SOC_3.3V | Power |
| 13 | G3 | GPIO38 (U0RXD) | Yes (strapping) | | 14 | G1 | GPIO37 (U0TXD) | Yes (strapping, boot text) |
| 15 | G13/RXD2 | GPIO7 | Yes | | 16 | G14/TXD2 | GPIO6 | Yes (STAMP) |
| 17 | G21/SYS_SDA | GPIO31 | Shared | | 18 | G22/SYS_SCL | GPIO32 | Shared |
| 19 | G32/EXT_SDA | GPIO3 | Yes | | 20 | G33/EXT_SCL | GPIO4 | Yes |
| 21 | G27 | GPIO2 | Yes | | 22 | G19 | GPIO48 | Yes |
| 23 | G2 | GPIO47 | Yes | | 24 | G0 | GPIO35 (BOOT) | No (strapping) |
| 25 | HPWR | SYS_VIN | Power | | 26 | G34/ADC | GPIO51 | Yes |
| 27 | HPWR | SYS_VIN | Power | | 28 | 5V | SYS_EXT5VO | Power |
| 29 | HPWR | SYS_VIN | Power | | 30 | BAT | VBAT_OUT | Power |

### ExtPort1 (J9, 2 x 5)

| Pin | Net | Free | | Pin | Net | Free |
|---|---|---|---|---|---|---|
| 1 | GND | Power | | 2 | SYS_VIN (HVIN) | Power |
| 3 | GND | Power | | 4 | GND | Power |
| 5 | SYS_EXT5VO (5 V) | Power | | 6 | SOC_3.3V | Power |
| 7 | GPIO0 | Yes | | 8 | GPIO1 | App: RF DECODER: the DATA of a receiver module (while the app is open) |
| 9 | GPIO49 | App: IR DECODER: the DATA of an IR receiver module; SERIAL MONITOR: RX | | 10 | GPIO50 | App: IR DECODER: the TX of an IR LED module; SERIAL MONITOR: TX |

### ExtPort2 (J7, RS-485, 1.25 mm 6-pin)

| Pin | Net |
|---|---|
| 1 | GND |
| 2 | SYS_VIN (HVIN) |
| 3 | RS-485 A (through FU2) |
| 4 | RS-485 B (through FU3) |
| 5 | GPIO31, the internal I2C SDA |
| 6 | GPIO32, the internal I2C SCL |

The RS-485 chip (SIT3088) uses GPIO20 (TX), GPIO21 (RX) and GPIO34 (DIR). A switch on the board adds the 120 R end
resistor. Our firmware does not use RS-485.

### PORT.A (J11, Grove)

| Pin | Net | Use in our firmware |
|---|---|---|
| 1 | GND | Ground |
| 2 | SYS_EXT5VO (5 V) | Power |
| 3 | GPIO53 | Not used |
| 4 | GPIO54 | Not used |

### The STAMP pads

| Pad | Net | | Pad | Net |
|---|---|---|---|---|
| TP1 | Not connected | | TP7 | SYS_INT5V |
| TP2 | GPIO46 | | TP8 | SOC_3.3V |
| TP3 | GPIO6 | | TP9 | GND |
| TP4 | GPIO31 (I2C SDA) | | TP10 | GPIO5 (SPI SCK) |
| TP5 | GPIO32 (I2C SCL) | | TP11 | GPIO18 (SPI MOSI) |
| TP6 | GPIO33 (SPI CS) | | TP12 | GPIO19 (SPI MISO) |

GPIO5, GPIO6, GPIO18 and GPIO19 are on the M5-Bus too. A STAMP module and an M5-Bus module cannot use them at the
same time.

### What is free

- ExtPort1: GPIO0, GPIO1, GPIO49, GPIO50.
- PORT.A: GPIO53, GPIO54.
- The M5-Bus: GPIO2, GPIO3, GPIO4, GPIO5, GPIO6, GPIO7, GPIO16, GPIO17, GPIO18, GPIO19, GPIO45, GPIO47, GPIO48,
  GPIO51, GPIO52 (and GPIO37/GPIO38 with the strapping caution).
- The internal I2C bus on M5-Bus pins 17/18 and ExtPort2 pins 5/6, with the address limits below.

Our firmware uses these Tab5 pins: GPIO22 (the backlight), GPIO23 (the touch interrupt), GPIO26 to GPIO30 (the I2S
audio), GPIO31/GPIO32 (I2C), GPIO39 to GPIO44 (the SD card), GPIO8 to GPIO13 and GPIO15 (the WiFi chip ESP32-C6 on
SDIO). None of them is on a connector, but GPIO31/GPIO32.

The internal I2C bus (GPIO31 SDA, GPIO32 SCL; `board_tab5.c`):

| Chip | Address |
|---|---|
| PI4IOE5V6408 (the IO expanders) | 0x43, 0x44 |
| GT911 (the touch, ILI9881C panel) or the ST touch (ST7121/ST7123 panel) | 0x14 or 0x55 |
| ES8388 (the speaker codec) | 0x10 |
| ES7210 (the microphone ADC) | 0x40 |
| BMI270 (the IMU) | 0x68 |
| INA226 (the battery monitor) | 0x41 |
| RX8130CE (the RTC) | 0x32 |

The camera (SC2356) is on this bus too. Our firmware does not use the camera. Do an I2C scan before you add a module.

### The cautions

- The ESP32-P4 strapping pins on the connectors: GPIO35 (BOOT, M5-Bus pin 24), GPIO37 and GPIO38 (UART0, M5-Bus pins 14
  and 13). Do not pull them at boot. The ROM sends its boot text on GPIO37.
- The 5 V line SYS_EXT5VO (M5-Bus pin 28, ExtPort1 pin 5, PORT.A pin 2) has power only when the IO expander 0x43 P2
  (EXT5V_EN) is high. Our firmware sets it high at the start (`board_tab5.c`).
- BAT (M5-Bus pin 30) is the 2S battery: 6.0 V to 8.2 V (the M5Stack page), not 3.0 V to 4.2 V as on the CoreS3. SYS_VIN (HVIN) is the high
  input voltage (6 V to 24 V on the RS-485 input). Do not connect them to a 5 V or a 3.3 V module.
- The M5 modules for the Core2 fit the M5-Bus, but the Tab5 GPIOs are different from the Core2 GPIOs. A module that
  needs a special pin of the Core2 (a DAC, an ADC) can need other code on the Tab5.

---

## 5. The Waveshare AMOLED-1.8

The AMOLED-1.8 has no pin header and no socket. The PCB has small test pads (Waveshare: "1 mm GPIO pads"). A module
needs wires soldered to the pads.

| Pad | Net | Use in our firmware |
|---|---|---|
| TP2 | VBUS (USB 5 V) | Power |
| TP3 | GND | Ground |
| TP4 | VCC3V3 | Power |
| TP5 | U0RXD (GPIO44) | Not used |
| TP6 | U0TXD (GPIO43) | Not used (boot text) |
| TP7 | ESP32_SCL (GPIO14) | The I2C bus |
| TP8 | ESP32_SDA (GPIO15) | The I2C bus |
| TP9 | GPIO41 | Not used |
| TP10 | GPIO42 | Not used |
| TP11 | GPIO17 | Not used |
| TP12 | GPIO18 | Not used |
| TP14 | GPIO38 | Not used |
| TP15 | GPIO39 | Not used |
| TP16 | GPIO40 | Not used |
| TP18 | GND | Ground |

The I2C bus (GPIO15 SDA, GPIO14 SCL; `board_amoled.c`): AXP2101 0x34, TCA9554 0x20, FT3168 (the touch) 0x38,
QMI8658 0x6B, ES8311 0x18 and the RTC 0x51.

---

## 6. The Elecrow CrowPanel Advance 7.0

The CrowPanel Advance 7.0 has a UART1/GPIO header and an I2C header on the back.

| Header | Pin | Net | Use in our firmware |
|---|---|---|---|
| GPIO / UART1 | GPIO8 | Free GPIO | `BOARD_FREE_PINS` (the 315 / 433 MHz radio and GPIO apps) |
| I2C | SDA | GPIO15 | The internal I2C bus (`board_crow7.c`) |
| I2C | SCL | GPIO16 | The internal I2C bus (`board_crow7.c`) |

The internal I2C bus (GPIO15 SDA, GPIO16 SCL): STC8H1K28 (backlight and power coprocessor) 0x30, GT911 (the touch) 0x5D or 0x14.

### The cautions

- GPIO19 and GPIO20 (USB D- and D+ on the ESP32-S3) are wired to board power rails and are driven high by `board_crow7.c`. The console uses UART0 over the onboard CH340 chip (`1a86:7523`), and USB MIDI / USB AUDIO stay off.
- The NS4168 speaker amplifier shares GPIO4, GPIO5, and GPIO6 with the microSD card SPI bus (`SPI2_HOST`). Our firmware leaves `BOARD_I2S_*` set to `GPIO_NUM_NC` so the audio driver does not take those pins from the SD card.
