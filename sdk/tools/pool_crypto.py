#!/usr/bin/env python3
"""The crypto of the pools (TFN OS P4 stage 2): the parts that the manager and pool_tool.py need.

Only the Python standard library (no extra package on the manager computer):
  X25519               RFC 7748 (the box keys and the ephemeral keys of a JOIN)
  ChaCha20-Poly1305    RFC 8439 (the 'A' 'P' beacon and the secret envelope)
  HKDF-SHA256          RFC 5869 (the keys made from the pool key, the seal key)
  SipHash-2-4          the tag of link v4 and of the UDP packets of version 2
ECDSA P-256 / SHA-256 (the roster, the JOIN signatures) uses the openssl program through fw_sign.py.

The code is for a few packets each second, not for speed. The board (main/tfn_pool_core.c) gives the same bytes:
tests/unit/vectors/pool_v1.json has the vectors of both sides.
"""
import hashlib
import hmac
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fw_sign  # noqa: E402

SignError = fw_sign.SignError


class CryptoError(Exception):
    pass


# ---------------------------------------------------------------- X25519 (RFC 7748)
P25519 = 2 ** 255 - 19
A24 = 121665


def _clamp(k):
    b = bytearray(k)
    b[0] &= 248
    b[31] &= 127
    b[31] |= 64
    return int.from_bytes(b, 'little')


def x25519(k, u):
    """The X25519 function: the 32-byte scalar k times the 32-byte u-coordinate u (RFC 7748 section 5)."""
    if len(k) != 32 or len(u) != 32:
        raise CryptoError('X25519 takes 32-byte values')
    s = _clamp(k)
    x1 = int.from_bytes(u, 'little') & ((1 << 255) - 1)
    x2, z2, x3, z3 = 1, 0, x1, 1
    swap = 0
    p = P25519
    for t in range(254, -1, -1):
        bit = (s >> t) & 1
        swap ^= bit
        if swap:
            x2, x3 = x3, x2
            z2, z3 = z3, z2
        swap = bit
        a = (x2 + z2) % p
        aa = a * a % p
        b = (x2 - z2) % p
        bb = b * b % p
        e = (aa - bb) % p
        c = (x3 + z3) % p
        d = (x3 - z3) % p
        da = d * a % p
        cb = c * b % p
        x3 = (da + cb) * (da + cb) % p
        z3 = x1 * (da - cb) * (da - cb) % p
        x2 = aa * bb % p
        z2 = e * (aa + A24 * e) % p
    if swap:
        x2, x3 = x3, x2
        z2, z3 = z3, z2
    return (x2 * pow(z2, p - 2, p) % p).to_bytes(32, 'little')


X25519_BASE = (9).to_bytes(32, 'little')


def x25519_pub(priv):
    return x25519(priv, X25519_BASE)


def x25519_keypair():
    """(private, public) of a new random X25519 key."""
    priv = os.urandom(32)
    return priv, x25519_pub(priv)


def x25519_shared(priv, pub):
    """The shared value of X25519. An all-zero value (a low-order point of an attacker) is refused."""
    z = x25519(priv, pub)
    if not any(z):
        raise CryptoError('X25519 gave the zero value (a bad public key)')
    return z


# ---------------------------------------------------------------- ChaCha20 and Poly1305 (RFC 8439)
def _rotl(v, c):
    return ((v << c) & 0xffffffff) | (v >> (32 - c))


def _quarter(s, a, b, c, d):
    s[a] = (s[a] + s[b]) & 0xffffffff
    s[d] = _rotl(s[d] ^ s[a], 16)
    s[c] = (s[c] + s[d]) & 0xffffffff
    s[b] = _rotl(s[b] ^ s[c], 12)
    s[a] = (s[a] + s[b]) & 0xffffffff
    s[d] = _rotl(s[d] ^ s[a], 8)
    s[c] = (s[c] + s[d]) & 0xffffffff
    s[b] = _rotl(s[b] ^ s[c], 7)


def chacha20_block(k, counter, nonce):
    """One 64-byte block of ChaCha20 (RFC 8439 section 2.3): a 32-byte key, a 32-bit counter, a 12-byte nonce."""
    if len(k) != 32 or len(nonce) != 12:
        raise CryptoError('ChaCha20 takes a 32-byte key and a 12-byte nonce')
    init = [0x61707865, 0x3320646e, 0x79622d32, 0x6b206574]
    init += list(struct.unpack('<8I', k)) + [counter & 0xffffffff] + list(struct.unpack('<3I', nonce))
    s = list(init)
    for _ in range(10):
        _quarter(s, 0, 4, 8, 12)
        _quarter(s, 1, 5, 9, 13)
        _quarter(s, 2, 6, 10, 14)
        _quarter(s, 3, 7, 11, 15)
        _quarter(s, 0, 5, 10, 15)
        _quarter(s, 1, 6, 11, 12)
        _quarter(s, 2, 7, 8, 13)
        _quarter(s, 3, 4, 9, 14)
    return struct.pack('<16I', *((s[i] + init[i]) & 0xffffffff for i in range(16)))


def chacha20_xor(k, counter, nonce, data):
    """ChaCha20 encryption (and decryption) of data from the block counter (RFC 8439 section 2.4)."""
    out = bytearray(len(data))
    for i in range(0, len(data), 64):
        ks = chacha20_block(k, counter + i // 64, nonce)
        part = data[i:i + 64]
        out[i:i + len(part)] = bytes(x ^ y for x, y in zip(part, ks))
    return bytes(out)


def poly1305(k, msg):
    """The 16-byte Poly1305 tag of msg with the one-time key k (32 bytes; RFC 8439 section 2.5)."""
    if len(k) != 32:
        raise CryptoError('Poly1305 takes a 32-byte key')
    r = int.from_bytes(k[:16], 'little') & 0x0ffffffc0ffffffc0ffffffc0fffffff
    s = int.from_bytes(k[16:], 'little')
    p = (1 << 130) - 5
    acc = 0
    for i in range(0, len(msg), 16):
        n = int.from_bytes(msg[i:i + 16] + b'\x01', 'little')
        acc = (acc + n) * r % p
    return ((acc + s) & ((1 << 128) - 1)).to_bytes(16, 'little')


def _pad16(n):
    return b'\0' * (-n % 16)


def _aead_tag(k, nonce, aad, ct):
    otk = chacha20_block(k, 0, nonce)[:32]
    mac = aad + _pad16(len(aad)) + ct + _pad16(len(ct)) + struct.pack('<QQ', len(aad), len(ct))
    return poly1305(otk, mac)


def aead_seal(k, nonce, plain, aad=b''):
    """ChaCha20-Poly1305 (RFC 8439 section 2.8): the cipher text followed by the 16-byte tag."""
    ct = chacha20_xor(k, 1, nonce, plain)
    return ct + _aead_tag(k, nonce, aad, ct)


def aead_open(k, nonce, sealed, aad=b''):
    """The plain text of aead_seal(), or None when the tag is wrong."""
    if len(sealed) < 16:
        return None
    ct, tag = sealed[:-16], sealed[-16:]
    if not hmac.compare_digest(_aead_tag(k, nonce, aad, ct), tag):
        return None
    return chacha20_xor(k, 1, nonce, ct)


# ---------------------------------------------------------------- HKDF-SHA256 (RFC 5869)
def hkdf_extract(salt, ikm):
    return hmac.new(salt or b'\0' * 32, ikm, hashlib.sha256).digest()


def hkdf_expand(prk, info, n):
    if n > 255 * 32:
        raise CryptoError('HKDF: the length is too big')
    out, t, i = b'', b'', 1
    while len(out) < n:
        t = hmac.new(prk, t + info + bytes([i]), hashlib.sha256).digest()
        out += t
        i += 1
    return out[:n]


def hkdf(ikm, salt, info, n):
    return hkdf_expand(hkdf_extract(salt, ikm), info, n)


# ---------------------------------------------------------------- SipHash-2-4
M64 = (1 << 64) - 1


def _rotl64(v, c):
    return ((v << c) & M64) | (v >> (64 - c))


def _sipround(v0, v1, v2, v3):
    v0 = (v0 + v1) & M64
    v1 = _rotl64(v1, 13) ^ v0
    v0 = _rotl64(v0, 32)
    v2 = (v2 + v3) & M64
    v3 = _rotl64(v3, 16) ^ v2
    v0 = (v0 + v3) & M64
    v3 = _rotl64(v3, 21) ^ v0
    v2 = (v2 + v1) & M64
    v1 = _rotl64(v1, 17) ^ v2
    v2 = _rotl64(v2, 32)
    return v0, v1, v2, v3


def siphash24(k, msg):
    """The 8 bytes of SipHash-2-4 of msg with the 16-byte key k (the 64-bit result, little endian)."""
    if len(k) != 16:
        raise CryptoError('SipHash takes a 16-byte key')
    k0, k1 = struct.unpack('<QQ', k)
    v0 = k0 ^ 0x736f6d6570736575
    v1 = k1 ^ 0x646f72616e646f6d
    v2 = k0 ^ 0x6c7967656e657261
    v3 = k1 ^ 0x7465646279746573
    n = len(msg)
    end = n - n % 8
    for i in range(0, end, 8):
        m = struct.unpack_from('<Q', msg, i)[0]
        v3 ^= m
        v0, v1, v2, v3 = _sipround(v0, v1, v2, v3)
        v0, v1, v2, v3 = _sipround(v0, v1, v2, v3)
        v0 ^= m
    last = (n & 0xff) << 56
    for i, c in enumerate(msg[end:]):
        last |= c << (8 * i)
    v3 ^= last
    v0, v1, v2, v3 = _sipround(v0, v1, v2, v3)
    v0, v1, v2, v3 = _sipround(v0, v1, v2, v3)
    v0 ^= last
    v2 ^= 0xff
    for _ in range(4):
        v0, v1, v2, v3 = _sipround(v0, v1, v2, v3)
    return struct.pack('<Q', v0 ^ v1 ^ v2 ^ v3)


# ---------------------------------------------------------------- the hashes, the HMAC, the random bytes
def sha256(data):
    return hashlib.sha256(data).digest()


def hmac_sha256(k, data):
    return hmac.new(k, data, hashlib.sha256).digest()


def same(a, b):
    """A compare in constant time (tags, MACs)."""
    return hmac.compare_digest(a, b)


def rand(n):
    return os.urandom(n)


# ---------------------------------------------------------------- ECDSA P-256 (the openssl program)
def p256_keygen(pem_path):
    """A new P-256 key pair in pem_path (mode 0600; an existing file stays). Returns the 65-byte public point."""
    return fw_sign.keygen(pem_path)


def p256_pub(pem_path):
    return fw_sign.pub_of_pem(pem_path)


def p256_sign(pem_path, msg):
    """r || s (64 bytes) of an ECDSA P-256 / SHA-256 signature of msg."""
    return fw_sign.sign_raw(pem_path, msg)


def p256_verify(pub, msg, rs):
    return len(pub) == 65 and pub[0] == 4 and len(rs) == 64 and fw_sign.verify_raw(pub, msg, rs)


def key_id(pub):
    """The key ID of an admin key in the roster signature: the first 8 bytes of SHA-256 of the 65-byte point (the
    key ID of the signed OTA too)."""
    return fw_sign.key_id(pub)


def device_id(pub):
    """The device ID (main/tfn_id.h): the first 8 bytes of SHA-256("TFNID1" || the 65-byte point)."""
    return sha256(b'TFNID1' + pub)[:8]
