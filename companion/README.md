# TFN COMPANION: the firmware of a companion

A companion is a small ESP32 board with no screen. It connects to a TFN OS board by ESP-NOW and becomes the "hands"
of that board: the modules on its pins (an IR receiver, an IR LED, 315 / 433 MHz receivers and transmitters, relays,
switch outputs, buttons, motion sensors, door contacts, analog sensors) do the work. The TFN OS board is the master:
its companion service keeps the paired companions; the app COMPANIONS pairs a companion, tells it which module is on
which GPIO and tests each module live; the app CONTROL has remotes with IR keys (learned from real remotes), SWITCH
keys (a relay or a light) and SENSOR tiles (the live value of an input).

The companion stays simple: it captures level times and sends level times, it sets an output, it reads an input. It
does not know the IR protocols. The TFN OS board decodes and encodes them (`libs/irdecode`). So a new protocol comes
with a TFN OS update.

## The modules (the role of each pin)

| Class | Roles | What the companion does |
| --- | --- | --- |
| Pulse receiver | IR RECEIVER, 315 RECEIVER, 433 RECEIVER | the live edges (WATCH), one frame of level times (CAPTURE) |
| Pulse transmitter | IR LED, 315 TRANSMITTER, 433 TRANSMITTER | a test burst (PULSE), the level times (SEND, a HOLD repeats) |
| Switch output | RELAY, OUTPUT (an LED, a buzzer, a MOSFET) | ON, OFF, a PRESS of N ms (SET); it starts OFF |
| Switch input | BUTTON, CONTACT (to GND, the internal pull-up), MOTION (PIR: HIGH) | the state and a count of the activations |
| Analog input | ANALOG (a light or soil sensor, a knob) | the value in mV (ADC1 only: on the ESP32 GPIO 32 to 39) |

INVERT (in COMPANIONS: ON AT LOW) turns the active level: many relay boards switch on at LOW. The companion sends the
values of the switch and analog modules at each change (an input: 30 ms of debounce; analog: a change of 40 mV), and
after each PONG of its master (each 2 s). The console: `mods` (the values), `set <pin> <0|1> [ms]`.

## The chips

| Chip | Build target | Status |
| --- | --- | --- |
| ESP32 (for example an ESP32-WROOM-32D DevKit V1) | `esp32` | Tested on a board |
| ESP32-S3 | `esp32s3` | Builds |
| ESP32-C3 | `esp32c3` | Builds |
| ESP32-C6 | `esp32c6` | Builds |

## The update by radio (firmware 3 and later)

The flash has two app slots (`partitions.csv`, 4 MB: `ota_0` and `ota_1` of 1.9 MB; the NVS with the pairing at the
same place as in the first layout). The TFN OS board sends a new firmware by radio. THE FIRMWARE COMES FROM THE STORE:
`python sdk/tools/mk_compfw.py` publishes the images of the four chips (`compfw/index.txt` with the version, the size
and the SHA-256 of each one, signed by the store key, and `compfw/<target>.bin`; `--out` writes them for a static
store). COMPANIONS asks the store of the board for the newest version of the chip of a companion and shows UPDATE TO
FIRMWARE N; the tap downloads the file into the memory (no SD card is needed), checks it, and sends it (MORE >
FIRMWARE N BY RADIO sends the same version again). With no WiFi, a file on the board is the other way: any name that
ends with `.bin` in `/sdcard/companion/` or `/data/companion/` (COMPANIONS reads the chip and the version from it). The frames: TC_OTA_BEGIN (the size, the CRC-32), TC_OTA_DATA (200 bytes, a window of 8, an
ACK with the next offset), TC_OTA_END (the companion checks the size, the CRC and the image, then starts again). A
new firmware that does not reach its master before the next start goes back to the old one (the rollback of the
bootloader). The version of the image = `CC_FW` of `main/comp_core.h` (the CMake file reads it).

A companion with firmware 1 or 2 (one app slot) needs one USB flash of this layout.

## Build and flash (ESP-IDF 6.1)

The build folder is `build_dev/companion_<target>`. By hand:

```
idf.py -B ../build_dev/companion_esp32 -D SDKCONFIG=../build_dev/companion_esp32/sdkconfig set-target esp32 build
```

## The wiring

- **An IR receiver module** (VS1838B, TSOP38238): VCC to **3V3**, GND to GND, OUT to a free GPIO. Do not supply it
  from 5 V: its output then goes above the 3.6 V limit of the pin.
- **An IR LED**: from a GPIO to GND (short range, about 1 to 2 m), or an IR LED module with a transistor (more range).
- Do not use these pins: the flash pins, the console pins (ESP32: GPIO 1 and 3), the USB pins of the S3 / C3 / C6.
- Do not use a strapping pin for a receiver (ESP32: GPIO 0, 2, 5, 12, 15). On many ESP32 DevKit boards, GPIO 2 also
  has the blue LED of the board: that LED holds a receiver line LOW.
- Good pins on the ESP32: GPIO 4, 13, 14, 16 to 19, 21 to 23, 25 to 27, 32, 33 (34 to 39: input only, receivers
  only).

The app COMPANIONS shows the pins of the chip of the companion and warns for each strapping pin.

## Pairing

1. Open COMPANIONS on the TFN OS board, then ADD A COMPANION. The board lists the companions that it hears.
2. Tap the companion. Its LED blinks fast.
3. Press the BOOT key of the companion within 60 s.
4. Give the companion a name, then ADD A MODULE for each module on its pins.

The two boards make a shared key (X25519). After the pairing, the companion takes only the frames of its master
(a SipHash tag and a counter on each frame). A hold of the BOOT key for 10 s makes the companion forget its master.

## The LED of the board

| LED | State |
| --- | --- |
| Blink, 0.7 s on and 0.7 s off | No master |
| Fast blink | A pairing waits for the BOOT key |
| On | The master is here |
| Blink, 1.5 s on and 1.5 s off | The companion searches for its master (the master is off, or on another channel) |
| Off for a moment | A frame goes out of the IR LED (a held button) |

A module on the pin of the LED turns the LED off.

## The channel

ESP-NOW works only when both boards are on the same WiFi channel. The master stays on the channel of its WiFi
network. The companion searches the channels until it hears its master, and then it follows the channel that the
master writes in each frame. When the master is silent for 6.5 s, the companion searches again.

## The test console (USB serial, 115200 baud)

| Command | What it does |
| --- | --- |
| `info` | The chip, the MAC, the master, the channel, the counters, the wiring |
| `wire <pin> <irrx\|irtx\|315rx\|315tx\|433rx\|433tx> [inv] [khz]` | Set one pin of the wiring (`wire del <pin>`, `wire clear`) |
| `pin <pin>` | The level of a pin with no pull, the pull-up and the pull-down (a wiring check) |
| `watch <pin> <secs>` | The edges of a receiver pin |
| `cap <pin> [secs]` | Capture one frame and decode it |
| `send <proto> <addr> <cmd> [pin]` | Send a code (for example `send nec 0x04 0x08`) |
| `selftest [proto]` | Send a code on the IR LED and capture it on the IR receiver |
| `boot` / `forget` | A press of the BOOT key / forget the master |

A serial terminal on the USB port of the companion (115200 baud) sends these commands.

## The files

- `main/comp_core.c`: the logic (the pairing, the channel search, the frames, the capture, the send, the repeat).
  Pure C: a PC test can run it with fakes of the hardware.
- `main/comp_hw.c`: the chip (ESP-NOW, the GPIO edge interrupts, the RMT, the NVS, the LED, the BOOT key).
- `main/main.c`: the start, the main loop, the test console.
- `../libs/tfncomp`: the protocol (the frames, the tags, the keys, the parts of long frames, the pin tables). TFN OS
  has the same files as a module (its companion service).
