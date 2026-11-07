#pragma once

// SHA-256 and ECDSA P-256 for the host unit tests (host_crypto.c): the board uses the PSA code of ESP-IDF, the host
// tests use this small copy. Slow and not constant time: only for tests.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void hc_sha256(const uint8_t *data, size_t n, uint8_t out[32]);
// the public key (0x04, X, Y) of the private scalar d (32 bytes, big endian, 1 .. n-1); false: d is out of range
bool hc_p256_pub(const uint8_t d[32], uint8_t pub[65]);
// an ECDSA P-256 / SHA-256 signature r || s of msg with d and the nonce k (32 bytes, 1 .. n-1)
bool hc_p256_sign(const uint8_t d[32], const uint8_t k[32], const uint8_t *msg, size_t n, uint8_t sig[64]);
// true: sig (r || s) is a good ECDSA P-256 / SHA-256 signature of msg by pub (a point on the curve)
bool hc_p256_verify(const uint8_t pub[65], const uint8_t *msg, size_t n, const uint8_t sig[64]);
