# esp_wireguard in the arcade project

A copy of https://github.com/trombik/esp_wireguard v0.9.0 (BSD-3-Clause, see LICENSE), with these
changes for ESP-IDF 6.1:

- `src/wireguard-platform.c`: the random bytes come from `esp_fill_random()` (the mbedtls
  entropy and ctr_drbg modules are not in ESP-IDF 6).
- `src/esp_wireguard.c`: the preshared key uses `wireguard_base64_decode()` (no mbedtls).
- `CMakeLists.txt`: no mbedtls; two GCC 15 warnings of the old code are not errors.

The examples folder is not copied. The arcade uses it in `main/vpn.c`.
