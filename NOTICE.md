# NOTICE

The [MIT License](LICENSE) applies to the code, the tools and the pixel art of this project.
It does not apply to the parts in this file. Each part has its own terms.
The code of other projects is in `third_party/<name>/`, each folder with its licence file.

## The font of STOCKS

- **File:** `libs/interfont/stocks_font.inc` (the glyph bitmaps, made by `libs/interfont/tools/export_inter.py` from the
  font file Inter).
- **Source:** [Inter](https://github.com/rsms/inter), Copyright 2020 The Inter Project Authors.
- **Licence:** the [SIL Open Font License 1.1](docs/licenses/Inter-OFL.txt). The glyph data in
  `libs/interfont/stocks_font.inc` is under the same licence.

## The fonts of STANDBY

- **File:** `libs/interfont/sb_fonts.inc` (the glyph bitmaps, made by `libs/interfont/tools/export_inter.py` from the
  font file Inter).
- **Source:** [Inter](https://github.com/rsms/inter), Copyright 2020 The Inter Project Authors.
- **Licence:** the [SIL Open Font License 1.1](docs/licenses/Inter-OFL.txt). The glyph data in
  `libs/interfont/sb_fonts.inc` is under the same licence.

## The BMI270 configuration file

- **File:** `third_party/bmi270/bmi270_config.c`: the configuration data of the Bosch Sensortec BMI270 IMU.
  The licence file: `third_party/bmi270/LICENSE`.
- **Source:** the [Bosch BMI270 Sensor API](https://github.com/boschsensortec/BMI270_SensorAPI),
  as copied in [M5Unified](https://github.com/m5stack/M5Unified) (MIT).
- **Licence:** BSD 3-Clause:

> Copyright (c) 2023 Bosch Sensortec GmbH. All rights reserved.
>
> Redistribution and use in source and binary forms, with or without modification, are
> permitted provided that the following conditions are met:
>
> 1. Redistributions of source code must retain the above copyright notice, this list of
>    conditions and the following disclaimer.
> 2. Redistributions in binary form must reproduce the above copyright notice, this list of
>    conditions and the following disclaimer in the documentation and/or other materials
>    provided with the distribution.
> 3. Neither the name of the copyright holder nor the names of its contributors may be used
>    to endorse or promote products derived from this software without specific prior
>    written permission.
>
> THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS
> OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
> MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
> COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
> EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
> SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
> HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
> TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
> SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

## The fonts of WEATHER

- **File:** `apps/weather/wx_fonts.inc` (the glyph bitmaps, made by
  a tool that is not in this tree from the font files Inter and Rubik).
- **Source:** [Inter](https://github.com/rsms/inter), Copyright 2020 The Inter Project Authors;
  [Rubik](https://github.com/googlefonts/rubik), Copyright 2015 The Rubik Project Authors.
- **Licence:** the [SIL Open Font License 1.1](docs/licenses/Inter-OFL.txt) and
  [docs/licenses/Rubik-OFL.txt](docs/licenses/Rubik-OFL.txt). The glyph data is under the same licence.

## The icons of WEATHER

- **File:** `apps/weather/wx_icons.inc` (the icons of the conditions, made by `apps/weather/tools/mk_icons.py`).
- **Source:** original pictures: the tool draws each icon with Pillow (shapes and gradients). No picture of another
  author is in them.
- **Licence:** the MIT License of this project.

## The weather data of WEATHER (Open-Meteo)

- **Files:** `apps/weather/wx_data.inc` asks the Open-Meteo APIs (https://open-meteo.com): the forecast
  (api.open-meteo.com), the air quality (air-quality-api.open-meteo.com) and the search of a city
  (geocoding-api.open-meteo.com). The app does not keep the data in this repo.
- **Source:** [Open-Meteo](https://open-meteo.com), weather data by Open-Meteo.com; the city search uses the data of
  [GeoNames](https://www.geonames.org).
- **Licence:** the data is under the [Creative Commons Attribution 4.0](https://creativecommons.org/licenses/by/4.0/)
  licence (CC BY 4.0): show the attribution "Weather data by Open-Meteo.com" with the data. The app shows this line at
  the end of the page of a place and at the bottom of the panel of the sky in the desk view. The free API is for a use
  that is not commercial (the terms of https://open-meteo.com/en/terms).

## The place of the IP address of WEATHER (ipwho.is)

- **Files:** `apps/weather/wx_data.inc` asks the free endpoint of ipwhois.io (https://ipwho.is, HTTPS, no key) for the
  place of the internet address of the board (MY PLACE): one request at each open of the app. `apps/weather/wx_roots.inc`
  has the root certificates of that host (GTS Root R1 and R4, ISRG Root X1 and X2, from the Mozilla list).
- **Source:** [ipwhois.io](https://ipwhois.io); the app does not keep the data in this repo.
- **Terms:** the free endpoint allows commercial use, with no key, at 1000 requests a day for each client IP address
  and with no SLA (https://ipwhois.io/documentation). When the request fails, the app uses Tel Aviv, and the user can
  choose a city (ADD A PLACE: the lists or SEARCH).

## minimp3 (the MP3 decoder)

- **File:** `third_party/minimp3/minimp3.h` (changed: a static work memory and a mono synthesis, marked "TFN OS").
- **Source:** [minimp3](https://github.com/lieff/minimp3) by lieff.
- **Licence:** CC0 1.0 (public domain); the notice is in the file header and in `third_party/minimp3/LICENSE`.

## zstd (the decoder of the ZIM reader)

- **Files:** `third_party/zstd/zstddeclib.c` (the single-file decoder, made by the tool of the release) and
  `third_party/zstd/zstd.h`.
- **Source:** [Zstandard](https://github.com/facebook/zstd) 1.5.6, Meta Platforms, Inc. and affiliates.
- **Licence:** BSD (`third_party/zstd/LICENSE`); the source also allows the GPLv2. Not changed.

## The method of tfphys (Box2D)

- **Files:** `libs/tfphys/tfphys.c` and `tfphys.h` (new code; the solver follows the "soft step" of Box2D v3, and two
  small functions follow it: the fast atan2 polynomial and the nearest points of two segments).
- **Source:** [Box2D](https://github.com/erincatto/box2d) by Erin Catto.
- **Licence:** the MIT License, Copyright (c) 2023 Erin Catto.

## The root certificates of tfnet

- **File:** `libs/tfnet/tn_roots.inc` (14 root certificates in PEM, made by a tool that is not in this tree).
- **Source:** the list of public root certificates of Mozilla, as curl publishes it (https://curl.se/ca/cacert.pem).
- **Licence:** the list is under the Mozilla Public License 2.0; the certificates are public data of their authorities.

## The root certificates of STANDBY

- **File:** `main/standby_roots.inc` (ISRG Root X1 and X2, GTS Root R1 and R4 in PEM: a copy of the roots of WEATHER).
- **Source:** the list of public root certificates of Mozilla, as curl publishes it (https://curl.se/ca/cacert.pem).
- **Licence:** the list is under the Mozilla Public License 2.0; the certificates are public data of their authorities.

## The Torah readings table of hebcal

- **File:** `libs/hebcal/hc_parsha.inc` (the weekly readings for each type of Hebrew year).
- **Source:** made with [pyluach](https://github.com/simlist/pyluach) by MS List.
- **Licence:** pyluach is under the MIT License; the table is calendar data.

## miniz (host tests only)

- **Files:** `tests/host/common/miniz.c` and `tests/host/common/miniz.h`.
- **Use:** the host tests use them to inflate data. The firmware uses the ROM inflate of
  the ESP32-S3 and does not use these files.
- **Licence:** miniz by Rich Geldreich: public domain (the Unlicense) and the MIT-style
  notice of RAD Game Tools and Valve Software that is in the file header.

## esp_wireguard (the VPN of the board)

- **Files:** `components/esp_wireguard/` (changed for ESP-IDF 6.1; the changes are in
  `components/esp_wireguard/ARCADE.md`).
- **Source:** [esp_wireguard](https://github.com/trombik/esp_wireguard) 0.9.0, Copyright (c) 2022 Tomoyuki Sakurai.
  It comes from the WireGuard implementation for lwIP, Copyright (c) 2021 Daniel Hope (www.floorsense.nz), and its
  ESP32 port, Copyright (c) 2021 Kenta Ida.
- **Licence:** BSD-3-Clause (`components/esp_wireguard/LICENSE`). The crypto code in
  `components/esp_wireguard/src/crypto/refc/` has its own notices: X25519, the MIT License, Copyright (c) 2015-2016
  Cryptography Research, Inc. (`x25519-license.txt` there); poly1305-donna (Andrew Moon), public domain or MIT;
  BLAKE2s, the reference code of RFC 7693.

## elf_loader (the loader of the app files)

- **Files:** `components/elf_loader/` (changed for TFN OS: the changes are marked "TFN OS" in
  `components/elf_loader/src/esp_elf.c`).
- **Source:** [espressif/elf_loader](https://github.com/espressif/esp-iot-solution/tree/master/components/elf_loader)
  1.3.5, Copyright 2023-2026 Espressif Systems (Shanghai) CO LTD.
- **Licence:** the Apache License 2.0 (`components/elf_loader/license.txt`).

## TJpgDec (the JPEG decoder on the Tab5)

- **Files:** `third_party/tjpgd/tjpgd_p4.c` (only for the ESP32-P4: the ESP32-S3 has TJpgDec in its ROM) and
  `main/tjpgd_rom.h`.
- **Source:** TJpgDec R0.03 (http://elm-chan.org/fsw/tjpgd/), (C) ChaN, 2021, from the copy in espressif/esp_jpeg.
- **Licence:** the licence of ChaN: free use, change and redistribution, with no warranty; the copyright notice must
  stay in the source. The notice is in the file header and in `third_party/tjpgd/LICENSE`.

## The panel start tables of the Tab5 and the CoreS3 (LovyanGFX)

- **Files:** `main/tab5_panels.inc` (the start tables of the Tab5 panels, from `Panel_ILI9881C.hpp`,
  `Panel_ST7121.hpp` and `Panel_ST7123.hpp` in `src/lgfx/v1/platforms/esp32p4/` of M5GFX) and the ILI9342C list
  `INIT_C` in `main/board_cores3.c` (from `Panel_ILI9342` of M5GFX).
- **Source:** [LovyanGFX](https://github.com/lovyan03/LovyanGFX) in [M5GFX](https://github.com/m5stack/M5GFX),
  Copyright (c) 2020 lovyan03 and the contributors.
- **Licence:** the FreeBSD License ([docs/licenses/LovyanGFX-FreeBSD.txt](docs/licenses/LovyanGFX-FreeBSD.txt)).

## The fonts of SERIAL MONITOR and I2C SCANNER

- **Files:** `apps/serial_monitor/ser_fonts.inc` and `apps/i2c_scanner/i2c_fonts.inc` (the glyph bitmaps, made from
  the font file JetBrains Mono).
- **Source:** [JetBrains Mono](https://github.com/JetBrains/JetBrainsMono), Copyright 2020 The JetBrains Mono
  Project Authors.
- **Licence:** the [SIL Open Font License 1.1](docs/licenses/JetBrainsMono-OFL.txt). The glyph data is under the same
  licence.

## The fonts of the menu

- **Files:** the glyph bitmaps in `main/menu_font.inc` (Bungee and Rubik) and `main/keeper_font.inc` (Rubik).
- **Source:** [Rubik](https://github.com/googlefonts/rubik), Copyright 2015 The Rubik Project Authors;
  [Bungee](https://github.com/djrrb/Bungee), Copyright 2023 The Bungee Project Authors.
- **Licence:** the SIL Open Font License 1.1 ([Rubik](docs/licenses/Rubik-OFL.txt),
  [Bungee](docs/licenses/Bungee-OFL.txt)). The glyph data is under the same licence.

## The fonts of CLOCK and MQTT

- **Files:** the glyph bitmaps in `libs/interfont/ck_fonts.inc` and `apps/mqtt/mq_fonts.inc` (Inter).
- **Source:** [Inter](https://github.com/rsms/inter), Copyright 2020 The Inter Project Authors.
- **Licence:** the [SIL Open Font License 1.1](docs/licenses/Inter-OFL.txt). The glyph data is under the same licence.

## TRIVIA: the pictures, the logo and the effects

## RIM RUNNER: the effects

## The web site (tfnos.xyz)

- **Files:** `site/vendor/esptool-js-0.7.0/` (the web flasher talks to the ROM bootloader with it) and the font files
  in `site/fonts/`. The page `site/licences.html` lists the same parts for the visitors of the site.
- **Source:** [esptool-js](https://github.com/espressif/esptool-js) 0.7.0, Copyright Espressif Systems;
  [Atkinson Hyperlegible Next](https://github.com/googlefonts/atkinson-hyperlegible-next), Copyright 2020-2024 The
  Atkinson Hyperlegible Next Project Authors; [Bricolage Grotesque](https://github.com/ateliertriay/bricolage),
  Copyright 2022 The Bricolage Grotesque Project Authors.
- **Licence:** esptool-js: the Apache License 2.0 (`site/vendor/esptool-js-0.7.0/LICENSE`), not changed. The fonts:
  the SIL Open Font License 1.1 (`site/fonts/OFL-atkinson-hyperlegible-next.txt`,
  `site/fonts/OFL-bricolage-grotesque.txt`).

## Components that the build downloads

These components are not in this repo. The ESP-IDF component manager downloads them, and
each one has its own licence:
- ESP-IDF (Apache 2.0);
- espressif/esp_codec_dev;
- espressif/qrcode;
- espressif/esp_tinyusb and espressif/tinyusb (USB MIDI in the MIDI app).

## Names

The project has no relation to the owners of any game, name or brand that this file or the README names.
