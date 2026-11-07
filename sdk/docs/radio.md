# Radio sources and the SDR app (TFN OS, ABI rev 11)

The SDR app (`apps/sdr`) is a spectrum analyzer. It does not know which radio gives it the data. A radio is a
**source** with a fixed contract (`libs/rfsrc/rfsrc.h`). This document tells how the parts fit, and how to add a new
radio (for example a sub-GHz chip on the pins of a board) with no change to the app.

## 1. The layers

Each layer knows only the layers under it. No layer knows a layer above it.

```
apps/sdr            the model (the request of the user), the engine task, the views, the controls
   |  uses
libs/sdrdsp         the plan of a measurement, the engine (tune, read, FFT, detector), the traces, the waterfall,
   |                the persistence, the channel plans                      (pure C: the PC tests use the same code)
   |  uses
libs/rfsrc          THE CONTRACT (rfsrc.h) and the sources: rfsrc_native.c, rfsrc_demo.c, (your radio)
   |  uses
main/rf_native.c    the native radio of TFN OS (the debug I/Q path of the ESP32-S3 receiver, the radio start)
libs/sdrdsp/sdr_fft the FFT and the windows (a leaf of the math: the demo source uses it too; it knows no source)
```

- The app writes only the model. The engine task reads the model, makes a new plan when the model changes, and writes
  the traces and the history under one lock. The views only read.
- The modules `rfsrc`, `sdrdsp` and `sdrfft` are in each firmware (all modules are). Their public names are free ABI
  names, except the names that reach the radio: `rfsrc_enum`, `rfsrc_find`, `rfsrc_native` and the `rfn_*` names that
  change the radio have the permission class `radio.rx` ("LISTENS TO THE RADIO (THE BOARD RESTARTS WITH NO WIFI)").

## 2. The contract of a source (`rfsrc.h`)

A source gives one of two kinds of data:

| Kind | What it gives | The spectrum | Examples |
|---|---|---|---|
| `RFSRC_IQ` | blocks of complex samples (I, Q) round a centre (the LO) | an FFT in `sdrdsp` | the ESP32-S3 radio, an SX1255 on I2S |
| `RFSRC_SWEEP` | the power at a list of frequencies (an RSSI sweep) | the points themselves | CC1101, SX127x, SX126x, nRF24 |

**The capabilities (`rfsrc_caps_t`)** tell the planner what the source can do. The planner and the app read only them:

| Field | Meaning |
|---|---|
| `id`, `name[2]` | a fixed short name (the store keys), the name for the user in English and Hebrew |
| `kind`, `flags` | `RFSRC_IQ` or `RFSRC_SWEEP`; `RFSRC_F_DEMO` (made-up data), `RFSRC_F_NOWIFI` (no WiFi while it runs), `RFSRC_F_DBM` (the levels are dBm, else dBFS) |
| `band[]` | the ranges of the centre (IQ) or of the points (SWEEP) |
| `rate_hz[]`, `usable_hz[]` | IQ: the sample rates, and for each one the flat band with no alias: LO +- usable |
| `dc_hz` | IQ: the band round the LO with the leak of the LO (the planner keeps the points out of it) |
| `max_pairs` | IQ: the longest block of `read_iq` |
| `step_hz`, `point_bw_hz`, `max_points` | SWEEP: the narrowest step, the width of one point (the channel filter), the most points of one call |
| `gain_name[][2]`, `gain_val[]`, `gain_db[]`, `gain_def` | the gain steps: the name, the value for `tune`, about the dB of the step against the default step (the engine takes it out of the levels: a new gain does not move the traces) |
| `tune_us` | about the time of one tune (the planner uses it to choose the fewest tunes) |
| `full_scale_db` | the level of a full-scale sine (0 for dBFS; the dBm of a calibrated source) |

**The operations (`rfsrc_ops_t`)**:

| Operation | Meaning |
|---|---|
| `state` | `RFSRC_READY`, `RFSRC_NEEDS_START` (a special start of the board first), `RFSRC_ABSENT` |
| `claim(app)`, `unclaim()` | do the special start (it can restart the board and open the app again) / end it when the app quits |
| `open(set)`, `tune(set, &centre)` | the first setup / a new setup (a new centre is the usual change: make it fast) |
| `read_iq(iq, n, &meta)` | IQ: n pairs (int16 I, Q; `rfsrc_t.full_scale` = the count of a full-scale sample); the meta has the real centre, the rate and an overload flag |
| `read_sweep(f0, step, n, db)` | SWEEP: the power of n points |
| `close`, `error(e)` | the source off; the text of an error |

Rules for a source:
- The I/Q sign: a signal above the LO has a positive frequency (the native radio gives the conjugate of the chip).
- The data of a block comes from one time (no gap inside a block). The planner can ask for many blocks a second.
- A source does not draw, does not keep settings and does not know the app.

## 3. The native radio (`main/rf_native.c`, `rfsrc_native.c`)

The receiver of the ESP32-S3 has a debug path that writes the raw samples of its ADC into internal RAM (found in 2026
by the ESPARGOS ESP-SDR and the eSpDR projects; Espressif does not document it). The receiver setup comes from eSpDR
(Zero-Clause BSD). No code of ESP-SDR (GPL-3.0) is in the project; only facts of its documents.

- **The samples**: 10-bit I and Q (-512..511), 80 or 16 million pairs a second, at most 16380 pairs a block. The LO:
  2210..2780 MHz (the PLL must lock; a step of about 458 Hz). The usable band: +-20 MHz at 80 Msps (the 40 MHz filter
  bank), +-5 MHz at 16 Msps (the 20 MHz bank). The gain: an index of the gain table (LOW 30, NORMAL 48, HIGH 62).
- **The fixed RAM block**: the engine of the chip writes only into the block 0x3FCD0000..0x3FCE0000 (64 KB). The
  firmware keeps the block out of the heap at the start (`SOC_RESERVE_MEMORY_REGION`) and gives it to the heap at once
  in a normal start (`heap_caps_add_region`): a normal start loses no RAM. `rf_guard.ld` stops a build whose static RAM
  reaches the block.
- **The radio start**: only a radio start keeps the block free. `rfn_restart(true, app)` writes a request into the RTC
  memory and restarts; the next start reads it once (a crash or a power off gives a normal start), keeps the block
  free, starts no WiFi and no VPN, and opens the app. The app quits: `unclaim` restarts the board normally (the WiFi
  comes back). The radio needs the WiFi receiver, so the WiFi is off while the radio listens.
- **The fast tune**: a full tune scans the 512 capacitor codes of the PLL (about 26 ms). The firmware keeps the code of
  each MHz that locked: the next tune to that MHz takes about 0.3 ms. A sweep visits the same centres again and again.
- **Spurs**: the board itself makes carriers at 2400.000 and 2440.000 MHz (the harmonics of the 40 MHz crystal, about
  -13 ppm off on one 3.49) and at 2448 MHz.
- **The console** (tests): `rf` (the state, the heap, the WiFi state), `rf boot [app]`, `rf exit`, `rf open <MHz>
  [gain] [80|16] [20|40]`, `rf set ...`, `rf tune <MHz>`, `rf cap [pairs]` (the DC, the RMS, the peaks), `rf scan <MHz>
  <MHz> <step> [gain]`, `rf shape [n]` (the mean noise spectrum: the shape of the filter), `rf fft [n]` (the time of
  one FFT).

## 4. The measurement (`sdrdsp`)

- **The plan** (`sdr_plan_make`, pure): the request (the centre, the span, the points = the width of the plot, the
  gain, the window, the detector, the resolution) and the capabilities give the rate, the FFT size, the FFTs of one
  block and the segments. **Offset tuning**: a segment uses ONE side of its LO, from LO + guard to LO + usable (the
  guard: the leak of the LO and 3 FFT bins). So the leak of the LO and the I/Q images of the other side are never in
  the picture, and no signal falls into a hole. The cost: twice the segments of a two-sided sweep. The 2.4 GHz band
  (84 MHz) at 80 Msps: 5 segments. A span under about 5 MHz: one block at 16 Msps.
- **The FFT size**: AUTO gives about two FFT bins a point; FINE 4 times finer, COARSE 4 times coarser. At most about
  32 K FFT points a frame (the time of a frame).
- **The engine** (`sdr_engine_frame`): for each segment a tune (only a new centre), a block, the mean taken out, the
  window, the FFTs (a mean of the power), the level in dB of the full scale less the dB of the gain step, the map of the
  bins to the points with the detector (PEAK, AVERAGE or SAMPLE). Float only: the FPU of the ESP32-S3 has no double.
- **The traces** (`sdr_trace`): LIVE, MAX (HOLD, or FALL: 20 dB a second), MIN, AVERAGE (the mean of the power of the
  last 4, 16 or 64 frames). A pan or a zoom moves the traces to the new axis (each new point takes the old points under
  it): the holds stay. A new gain starts them again. The floor of the noise comes from the MIN trace (+6 dB).
- **The history** (`sdr_wf`): the waterfall ring (a byte a point, 0.5 dB steps; each row keeps its own axis) and the
  persistence map (how often each level came at each point, with a fade).
- **The channel plans** (`sdr_chan`): WiFi 2.4 GHz, Bluetooth LE advertising, Zigbee / Thread, ISM 433 MHz. A plan is
  data: a new band = a new list.

## 5. How to add a radio on the pins (example: a CC1101 as a SWEEP source)

1. A new file `libs/rfsrc/rfsrc_cc1101.c` with one `rfsrc_t`:
   - the caps: `kind = RFSRC_SWEEP`, `flags = RFSRC_F_DBM`, the bands (300-348, 387-464, 779-928 MHz), `step_hz`
     (the smallest channel step), `point_bw_hz` (the RX filter, for example 58 kHz), `max_points`, `tune_us` (the time
     of one RSSI read with the PLL settle), `full_scale_db = 0`, the gain steps (or one step "FIXED");
   - `state`: `RFSRC_ABSENT` when the chip does not answer on SPI (read its PARTNUM), else `RFSRC_READY`;
   - `read_sweep`: for each point: set the frequency, wait for the PLL and the RSSI, read the RSSI register, convert it
     to dBm. Return the count.
2. The pins: the board and the pins of the radio come from the settings of the app (or a config file of the board),
   not from the app code.
3. One line in `rfsrc_enum()` (`rfsrc.c`): the source is in the list when `state` is not `RFSRC_ABSENT`.
4. The names that the file calls (`spi_device_*`, `gpio_*`) have their own permission classes (`bus`, `gpio`).
5. A channel plan for its band in `sdr_chan.c` (the 433 MHz plan is there; 868 / 915 MHz plans are the next).
6. Test it on the PC first: a host test of the DSP with a test source (tones of known levels) and the SWEEP
   demo (`rfsrc_demo_sweep`, the source of the tests), and a picture test of the app with the SWEEP demo.

The app does not change: the source list, the GAIN item, the units (dBm), the views and the plan follow the caps. An
I/Q chip (for example an SX1255 on I2S) is an `RFSRC_IQ` source with its rates and usable bands.

## 6. The tests

- The host tests of the project (not in this repo): `dsp_test` (the FFT, the level of a tone, the plans of each
  span, the level over the segments of a sweep, the traces, the peaks, the waterfall, both demos: DSP OK); `./look349.exe all`
  and `./looks3.exe all` (the app: pictures of each view, the gestures, the buttons, each panel value, AUTO GAIN, the
  frequency entry, the SWEEP source, 3000 fuzz steps: SDR OK).
- On a board: `rf_spike.py <COM>` (the native radio from the console), `sdr_board.py <COM> [b349|bs3]` (the card,
  START, the radio start, a picture of each view, the zoom, the exit), `sdr_prof.py <COM> [s] [steps]` (the speed:
  the app logs "sdr: N frames in 5 s: frame, tune, read, fft, map, post, renders").

## 7. Pulse receivers (`libs/rfpulse`) and RF DECODER

A module that demodulates by itself (a 315 / 433 MHz receiver: OOK and ASK; later a transceiver chip in its data mode:
also FSK) gives a logic level on a pin. That is the second contract (`libs/rfpulse/rfpulse.h`), next to the spectrum
contract of section 2: a receiver gives a stream of level times (> 0 high, < 0 low, in microseconds).

```
apps/rf_decoder     the wiring sketch, the live pulses, the list of the codes, a code
   |  uses
libs/rfdecode       the slicer (a stream into packets) and the decoders (RCSwitch, PWM, PPM, Manchester, NRZ), the
   |                encoders; pure C
libs/rfpulse        THE CONTRACT (rfpulse.h) and the receivers: on the pin of the board (rfp_gpio.c), the demo
                    (rfp_demo.c: it uses the encoders of rfdecode); the wiring of each board (rfp_board.h)
```

- The caps (`rfp_caps_t`): the modulations that the receiver can demodulate (`RFP_MOD_OOK`, `ASK`, `FSK`), the
  frequencies that the user can choose (a module is made for one of them), `RFP_F_WIRED` (the app shows the wiring
  first), `RFP_F_DEMO`.
- The ops: `open`, `read` (the level times since the last read), `stats` (the edges, the level now, the time of the last
  edge, the state of the line: QUIET, HIGH, ALIVE), `close`, `error`.
- The wiring (`rfp_board.h`): the connector of each board kind as data: its pads in rows, the role of each pad (3.3 V,
  GND, DATA), the GPIO of DATA, how the app must draw it. A new board = one entry; the free pins come from
  `docs/headers.md` of the repo root (and a new use goes back into that file).
- A new receiver (for example a CC1101 in its asynchronous mode, with its data pin): one new file with one `rfp_src_t`
  and one line in `rfp_enum()`. A chip like the CC1101 can be a source of both contracts: a SWEEP source for SDR and a
  pulse receiver for RF DECODER. The apps do not change.
- The tests (host tests of the project, not in this repo): `dec_test` (each RCSwitch protocol, PWM, PPM,
  Manchester with noise, jitter and glitches, noise alone, the text, the demo: DECODE OK; `./look349.exe all`, `./looks3.exe all`: the
  app with a fake wired receiver: the wiring states, CONNECTED, the five devices of the demo, a code, the decoders, the
  scroll, a fuzz: RF DECODER OK; `rf_board.py <port> <b349|bs3>` on a board).

## 8. Infrared receivers and IR DECODER (ABI rev 13)

An IR receiver module (a TSOP38238 or a VS1838B) removes the 36..40 kHz carrier and gives the frame as a logic level on
a pin: the same contract as section 7. Its output is LOW while it sees the carrier, so its receiver gives the levels
the other way round (> 0 = a mark) and has the caps flag `RFP_F_IR`.

```
apps/ir_decoder     the wiring sketch (two modules), the live marks, the list of the codes, a code, SEND
   |  uses
libs/irdecode       the slicer (a stream into frames: a space of 8 ms ends a frame), the decoders in the style of
   |                IRremote (NEC ... WHYNTER, the universal decoder, the hash of UNKNOWN), the encoders; pure C
libs/rfpulse        rfp_enum_ir(): the IR receiver on the IR pin (rfp_ir(), the code of rfp_gpio.c with the pull-up
                    and the inverted levels), the IR demo (rfp_irdemo.c); rfp_ir_send(): the IR LED on the TX pin (the
                    RMT makes the carrier: rfp_irtx.c); rfp_board_ir(): the pins (RFP_PAD_DATA, RFP_PAD_TX)
```

- The pins: the connector data of section 7 with other roles (`rfp_board_ir()`); `rfp_conn_t` has `tx_gpio` and
  `tx_pin` at its end (-1 and "" for the RF receiver). The RF receiver and the IR modules can stay connected together
  (other pins), but on the Watcher: J2 has two free pins (DATA = GPIO19 for both).
- `rfp_ir_send(d, n, carrier_hz)`: the level times of one or more frames (a gap between frames is a space); it waits
  for the end; the class `gpio`.
- The tests (host tests of the project, not in this repo): `dec_test` (DECODE OK; `./look349.exe all`,
  `./looks3.exe all`, `./lookwt.exe all`: IR DECODER OK; `ir_board.py <port> [<TX port> <TX GPIO>]` on a 3.49: the app, the demo, SEND, and
  RF DECODER with the transmitter board).
