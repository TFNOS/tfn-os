#!/usr/bin/env python3
"""The signature of a firmware file for the signed OTA: the trailer "TFNOTA1" (main/ota_sig.h).

The signed file = the ESP-IDF image as the build makes it (<kind>.bin, tfn_os.bin) + 1024 bytes of trailer. The
board (main/ota.c) checks the trailer at each update over the network. An old firmware writes the trailer into the
slot after the image and never reads it, so one file works on each board.

Only the Python standard library and the openssl program. Set OPENSSL to the
path of the program when it is not on the PATH.

Commands:
  keygen <out.pem>                          make a P-256 key pair with the named curve prime256v1 (the file gets mode
                                            0600); writes the public key as the line for main/tfn_local.h and its
                                            key ID
  pub <pem>                                 the public key line and the key ID of a private or a public PEM (the
                                            named curve, or explicit parameters with the values of P-256)
  sign <bin> --key <pem> [--key <pem2>] [--kind K] [--version V] [--sec-ver N] [--time T] [--out <file>]
                                            add the trailer (each --key adds a signature, up to 4). The kind comes
                                            from the board tag of the image and the version from the image (0x30);
                                            --kind / --version must agree with them.
  verify <bin> --pub <key> [--pub ...] [--kind K] [--minsv N]
                                            the check of the board; prints "ok:<key ID>" or the code (sig_none ...)
                                            exit 0 = ok, 1 = a fault
  check-backup <pem> --pub <key>            sign a test text with the private key and verify it with the public key
                                            (the check of a restored copy of a key)
A public key <key> is a PEM file (private or public), a file with the hex text (for example pubs/ota.pub of the release
workflow: 130 hex digits on one line), 130 hex digits, or the C bytes "0x04, 0x..". The content of a file tells its
form.
A key never goes into git, a log or a web answer. This tool prints only public values.
"""
import argparse
import hashlib
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time

TRAILER = 1024
SIGNED = 128
MAX_SIG = 4
MAGIC = b'TFNOTA1\0'
FORMAT = 1
TAG = b'@ARCADE-BOARD:'
DESC_VER = 0x30
CODES = ('ok', 'sig_none', 'sig_form', 'sig_hash', 'sig_kind', 'sig_key', 'sig_bad', 'sig_old', 'sig_err')
# the order of the P-256 group (the check of r and s)
N = 0xFFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551


# the creationflags of openssl: no console window on Windows (a service with pythonw.exe has no console); 0 else
NO_WINDOW = getattr(subprocess, 'CREATE_NO_WINDOW', 0)


class SignError(Exception):
    pass


# ---------------------------------------------------------------- the openssl program
def openssl():
    exe = os.environ.get('OPENSSL') or shutil.which('openssl')
    if not exe:
        for p in (r'C:\Program Files\Git\mingw64\bin\openssl.exe', r'C:\Program Files\Git\usr\bin\openssl.exe'):
            if os.path.exists(p):
                return p
        raise SignError('no openssl program (set OPENSSL)')
    return exe


def run_ssl(args, data=None):
    try:
        r = subprocess.run([openssl()] + args, input=data, capture_output=True, timeout=60, creationflags=NO_WINDOW)
    except (OSError, subprocess.SubprocessError) as e:
        raise SignError('openssl %s: %s' % (args[0], e))
    if r.returncode:
        raise SignError('openssl %s: %s' % (args[0], r.stderr.decode('utf-8', 'replace').strip()[-300:]))
    return r.stdout


# ---------------------------------------------------------------- the keys
def der_len(b, i):
    """The length of a DER item at b[i] (the length byte) -> (length, the start of the content)."""
    n = b[i]
    if n < 0x80:
        return n, i + 1
    k = n & 0x7f
    return int.from_bytes(b[i + 1:i + 1 + k], 'big'), i + 1 + k


def der_take(b, i, tag):
    """The content of the DER item at b[i], which must have the tag -> (the content, the index after the item).
    SignError for another tag, a length of more than 2 bytes or of no fixed size, or an item past the end of b."""
    if i + 2 > len(b) or b[i] != tag:
        raise SignError('no DER item %02x at %d' % (tag, i))
    if b[i + 1] == 0x80 or b[i + 1] > 0x82:
        raise SignError('a DER length form that a key does not use')
    n, s = der_len(b, i + 1)
    if s + n > len(b):
        raise SignError('a DER item goes past the end')
    return b[s:s + n], s + n


def der_uint(v):
    """The DER content of the INTEGER v (v >= 0): a 0 byte comes first when the top bit is 1."""
    return v.to_bytes(v.bit_length() // 8 + 1, 'big')


# the start of a P-256 SubjectPublicKeyInfo (DER): the algorithm id-ecPublicKey with the named curve prime256v1
# (1.2.840.10045.3.1.7), then the BIT STRING of the 65-byte point
SPKI_P256 = bytes.fromhex('3059301306072a8648ce3d020106082a8648ce3d030107034200')
OID_EC_KEY = bytes.fromhex('2a8648ce3d0201')  # id-ecPublicKey (1.2.840.10045.2.1)
OID_PRIME_FIELD = bytes.fromhex('2a8648ce3d0101')  # prime-field (1.2.840.10045.1.1)
PARAMS_NAMED = bytes.fromhex('06082a8648ce3d030107')  # the parameters "the named curve prime256v1"
# the values of the curve P-256 (SEC 2 secp256r1): the explicit parameters of a key must have these. The order is N.
P256_P = 0xFFFFFFFF00000001000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFF
P256_A = P256_P - 3
P256_B = 0x5AC635D8AA3A93E7B3EBBD55769886BC651D06B0CC53B0F63BCE3C3E27D2604B
P256_G = bytes.fromhex('046B17D1F2E12C4247F8BCE6E563A440F277037D812DEB33A0F4A13945D898C296'
                       '4FE342E2FE1A7F9B8EE7EB4A7C0F9E162BCE33576B315ECECBB6406837BF51F5')
P256_SEED = bytes.fromhex('c49d360886e704936a6678e1139d26b7819f7e90')


def p256_params(b):
    """True: b (the DER of the parameters of id-ecPublicKey) is the curve P-256. That is the named curve prime256v1, or
    the explicit ECParameters (SEC 1, C.2) with the values of P-256: version 1, the prime field of p, a, b, the
    uncompressed base point G, the order n and the cofactor 1. The seed and the cofactor are optional: a seed must be
    the seed of P-256."""
    if b == PARAMS_NAMED:
        return True
    seq, end = der_take(b, 0, 0x30)
    ver, i = der_take(seq, 0, 0x02)
    field, i = der_take(seq, i, 0x30)
    curve, i = der_take(seq, i, 0x30)
    base, i = der_take(seq, i, 0x04)
    order, i = der_take(seq, i, 0x02)
    cofactor = b'\x01'
    if i < len(seq):
        cofactor, i = der_take(seq, i, 0x02)
    ftype, f = der_take(field, 0, 0x06)
    prime, f = der_take(field, f, 0x02)
    a, c = der_take(curve, 0, 0x04)
    bb, c = der_take(curve, c, 0x04)
    seed = b'\x00' + P256_SEED
    if c < len(curve):
        seed, c = der_take(curve, c, 0x03)
    return (end == len(b) and i == len(seq) and f == len(field) and c == len(curve) and ver == b'\x01' and
            ftype == OID_PRIME_FIELD and prime == der_uint(P256_P) and a == P256_A.to_bytes(32, 'big') and
            bb == P256_B.to_bytes(32, 'big') and seed == b'\x00' + P256_SEED and base == P256_G and
            order == der_uint(N) and cofactor == b'\x01')


def point_from_spki(der):
    """The 65-byte uncompressed point of a P-256 SubjectPublicKeyInfo (DER). The curve is the named curve prime256v1,
    or explicit parameters with the values of P-256 (an openssl can write a key so: for example genpkey of LibreSSL with
    no ec_param_enc). A key of another curve (secp256k1, brainpoolP256r1 ...) also ends with a 65-byte point: only a
    P-256 key is a key of the boards."""
    try:
        spki, end = der_take(der, 0, 0x30)
        alg, i = der_take(spki, 0, 0x30)
        bits, i = der_take(spki, i, 0x03)
        oid, k = der_take(alg, 0, 0x06)
        good = (end == len(der) and i == len(spki) and oid == OID_EC_KEY and p256_params(alg[k:]) and
                len(bits) == 66 and bits[:2] == b'\x00\x04')
    except SignError:
        good = False
    if not good:
        raise SignError('not an uncompressed P-256 public key (the curve prime256v1)')
    return bits[1:]


def named_spki(der):
    """True: der is the short form of a P-256 SubjectPublicKeyInfo (the named curve prime256v1, 91 bytes)."""
    return len(der) == len(SPKI_P256) + 65 and der.startswith(SPKI_P256) and der[len(SPKI_P256)] == 4


def spki_from_point(pt):
    """A P-256 SubjectPublicKeyInfo (DER) for a 65-byte point."""
    return SPKI_P256 + pt


def pub_of_pem(pem_path):
    """The 65-byte public point of a private or a public PEM file."""
    with open(pem_path, 'rb') as h:
        text = h.read()
    if b'PUBLIC KEY' in text and b'PRIVATE' not in text:
        der = run_ssl(['pkey', '-pubin', '-in', pem_path, '-outform', 'DER'])
    else:
        der = run_ssl(['pkey', '-in', pem_path, '-pubout', '-outform', 'DER'])
    return point_from_spki(der)


def pub_of_hex(text, what):
    """The 65-byte point of 130 hex digits or of the C bytes "0x04, 0x..". what names the input in a message (a
    message never shows the text: the input can hold another value than a public key)."""
    hexs = re.sub(r'0x|[\s,]', '', text)
    try:
        pt = bytes.fromhex(hexs)
    except ValueError:
        raise SignError('%s is not a public key (130 hex digits that start with 04, or a PEM file)' % what)
    if len(pt) != 65 or pt[0] != 4:
        raise SignError('%s: a public key has 65 bytes and starts with 04' % what)
    return pt


def parse_pub(text):
    """A public key from the command line: a file (a PEM file, or a file with the hex text), 130 hex digits, or the C
    bytes "0x04, 0x..". The content of a file tells its form: a PEM header, else the hex text."""
    if os.path.isfile(text):
        with open(text, 'rb') as h:
            data = h.read()
        if b'-----BEGIN' in data:
            return pub_of_pem(text)
        try:
            body = data.decode('ascii')
        except UnicodeDecodeError:
            raise SignError('%s is not a PEM file and not the hex text of a public key' % text)
        return pub_of_hex(body, 'the file %s' % text)
    if '/' in text or '\\' in text or text.lower().endswith(('.pem', '.pub', '.hex', '.txt')):
        raise SignError('no file %s' % text)
    return pub_of_hex(text, 'the key text (%d characters)' % len(text))


def key_id(pt):
    return hashlib.sha256(pt).digest()[:8]


def c_bytes(pt):
    return ', '.join('0x%02x' % b for b in pt)


def new_key():
    """A new P-256 private key as PEM bytes (no file), with the named curve prime256v1. With no option, an openssl can
    write the curve in full (explicit parameters: for example genpkey of LibreSSL), and some tools (the Python package
    "cryptography") refuse such a key. So the commands ask for the named curve, and a key with another form stops."""
    try:
        pem = run_ssl(['genpkey', '-algorithm', 'EC', '-pkeyopt', 'ec_paramgen_curve:P-256', '-pkeyopt',
                       'ec_param_enc:named_curve'])
    except SignError:  # (an openssl with no EC in genpkey or no ec_param_enc, for example an old LibreSSL)
        pem = run_ssl(['ecparam', '-name', 'prime256v1', '-param_enc', 'named_curve', '-genkey', '-noout'])
    if not named_spki(run_ssl(['pkey', '-pubout', '-outform', 'DER'], pem)):
        raise SignError('openssl made a key with explicit curve parameters, not the named curve prime256v1')
    return pem


def keygen(out):
    if os.path.exists(out):
        raise SignError('%s exists: a key is never overwritten' % out)
    pem = new_key()
    fd = os.open(out, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, 'wb') as h:
        h.write(pem)
    return pub_of_pem(out)


# ---------------------------------------------------------------- the signatures
def der_to_rs(der):
    """An ECDSA DER signature -> r || s (64 bytes)."""
    if der[0] != 0x30:
        raise SignError('not a DER signature')
    _, i = der_len(der, 1)
    out = b''
    for _ in range(2):
        if der[i] != 2:
            raise SignError('not a DER signature')
        n, i = der_len(der, i + 1)
        v = der[i:i + n].lstrip(b'\0')
        if len(v) > 32:
            raise SignError('not a P-256 signature')
        out += v.rjust(32, b'\0')
        i += n
    return out


def rs_to_der(rs):
    def item(v):
        v = v.lstrip(b'\0') or b'\0'
        if v[0] & 0x80:
            v = b'\0' + v
        return b'\x02' + bytes([len(v)]) + v
    body = item(rs[:32]) + item(rs[32:])
    return b'\x30' + bytes([len(body)]) + body


def sign_raw(pem_path, msg):
    """r || s of an ECDSA P-256 / SHA-256 signature of msg."""
    return der_to_rs(run_ssl(['dgst', '-sha256', '-sign', pem_path], msg))


def verify_raw(pt, msg, rs):
    """True: rs is a good signature of msg by the point pt (the openssl program). A SignError when the check cannot
    run (no openssl, openssl does not start or does not end in time, the temporary files cannot be written)."""
    r, s = int.from_bytes(rs[:32], 'big'), int.from_bytes(rs[32:], 'big')
    if not (0 < r < N and 0 < s < N):
        return False
    exe = openssl()
    try:
        with tempfile.TemporaryDirectory() as d:
            kp, sp, mp = os.path.join(d, 'k.der'), os.path.join(d, 's.der'), os.path.join(d, 'm.bin')
            with open(kp, 'wb') as h:
                h.write(spki_from_point(pt))
            with open(sp, 'wb') as h:
                h.write(rs_to_der(rs))
            with open(mp, 'wb') as h:
                h.write(msg)
            r = subprocess.run([exe, 'dgst', '-sha256', '-verify', kp, '-keyform', 'DER', '-signature', sp, mp],
                               capture_output=True, timeout=60, creationflags=NO_WINDOW)
    except (OSError, subprocess.SubprocessError) as e:
        raise SignError('openssl (the check of a signature): %s' % e)
    return r.returncode == 0


# ---------------------------------------------------------------- the image
def image_len(data):
    """The length of the ESP-IDF image at the start of data (as esp_image_get_metadata: the header, the segments, the
    checksum block of 16 bytes and the appended SHA-256)."""
    if len(data) < 24 or data[0] != 0xE9:
        raise SignError('not an ESP-IDF image (the first byte is not E9)')
    nseg = data[1]
    pos = 24
    for _ in range(nseg):
        if pos + 8 > len(data):
            raise SignError('the image is cut (segment header at %d)' % pos)
        _, ln = struct.unpack_from('<II', data, pos)
        pos += 8 + ln
    pos = (pos + 1 + 15) & ~15
    if data[23] == 1:
        pos += 32
    if pos > len(data):
        raise SignError('the image is cut (%d bytes, the image needs %d)' % (len(data), pos))
    return pos


def image_kinds(data):
    """The board kinds of the board tags in the image."""
    return sorted({m.group(1).decode() for m in re.finditer(re.escape(TAG) + rb'([a-z0-9]{1,15})@', data)})


def image_version(data):
    return data[DESC_VER:DESC_VER + 32].split(b'\0')[0].decode('ascii', 'replace')


def has_trailer(data):
    return len(data) >= TRAILER and data[-TRAILER:-TRAILER + 8] == MAGIC


def fixed(text, n):
    b = text.encode('ascii')
    if len(b) > n:
        raise SignError('%r is longer than %d bytes' % (text, n))
    return b.ljust(n, b'\0')


def make_trailer(image, kind, version, sec_ver, t, keys):
    """The trailer of image with a signature of each private key (PEM paths)."""
    if not 1 <= len(keys) <= MAX_SIG:
        raise SignError('1 to %d keys' % MAX_SIG)
    if len(fixed(kind, 16).rstrip(b'\0')) > 15:
        raise SignError('the kind has at most 15 letters')
    head = bytearray(SIGNED)
    head[0:8] = MAGIC
    struct.pack_into('<HHI', head, 8, FORMAT, 0, len(image))
    head[16:48] = hashlib.sha256(image).digest()
    head[48:64] = fixed(kind, 16)
    head[64:96] = fixed(version, 32)
    struct.pack_into('<IQB', head, 96, sec_ver, t, len(keys))
    tr = bytearray(TRAILER)
    tr[:SIGNED] = head
    for i, pem in enumerate(keys):
        pt = pub_of_pem(pem)
        rs = sign_raw(pem, bytes(head))
        if not verify_raw(pt, bytes(head), rs):
            raise SignError('the new signature of %s does not verify' % pem)
        o = SIGNED + 72 * i
        tr[o:o + 8] = key_id(pt)
        tr[o + 8:o + 72] = rs
    return bytes(tr)


def sign_file(path, keys, kind=None, version=None, sec_ver=0, t=None, out=None):
    with open(path, 'rb') as h:
        data = h.read()
    if has_trailer(data):
        raise SignError('%s has a trailer already' % path)
    n = image_len(data)
    if n != len(data):
        raise SignError('%s: the file has %d bytes, the image %d: not a plain image of a build' % (path, len(data), n))
    kinds = image_kinds(data)
    if kind is None:
        if len(kinds) != 1:
            raise SignError('the image has the board tags %s: give --kind' % (kinds or 'none'))
        kind = kinds[0]
    elif kind not in kinds:
        raise SignError('the image has no board tag of %s (tags: %s)' % (kind, kinds or 'none'))
    iv = image_version(data)
    if version is None:
        version = iv
    elif version != iv:
        raise SignError('the version of the image is %r, not %r' % (iv, version))
    tr = make_trailer(data, kind, version, sec_ver, int(time.time()) if t is None else t, keys)
    dst = out or path
    tmp = dst + '.tmp'
    with open(tmp, 'wb') as h:
        h.write(data + tr)
    os.replace(tmp, dst)
    return kind, version


# ---------------------------------------------------------------- the check of the board (main/ota_sig.c)
def field_is(f, want):
    w = want.encode('ascii', 'replace')
    limit = len(f) if len(f) == 32 else len(f) - 1
    return len(w) <= limit and f == w.ljust(len(f), b'\0')


def check(data, pubs, kind, version=None, minsv=0, verify=verify_raw):
    """The result of the check of the board for the file data: (code, key ID or None, sec_ver or None)."""
    if len(data) < TRAILER or data[-TRAILER:-TRAILER + 8] != MAGIC:
        return 'sig_none', None, None
    tr = data[-TRAILER:]
    fmt, _, ln = struct.unpack_from('<HHI', tr, 8)
    nsig = tr[108]
    if fmt != FORMAT or ln != len(data) - TRAILER or not 1 <= nsig <= MAX_SIG or any(tr[109:128]):
        return 'sig_form', None, None
    sv = struct.unpack_from('<I', tr, 96)[0]
    if tr[16:48] != hashlib.sha256(data[:-TRAILER]).digest():
        return 'sig_hash', None, sv
    if version is None:
        version = image_version(data)
    if not field_is(tr[48:64], kind) or not field_is(tr[64:96], version):
        return 'sig_kind', None, sv
    ids = [(key_id(p), p) for p in pubs[:4] if p and p[0] == 4]
    named = False
    for i in range(nsig):
        s = tr[SIGNED + 72 * i:SIGNED + 72 * (i + 1)]
        for kid, p in ids:
            if s[:8] != kid:
                continue
            named = True
            if verify(p, tr[:SIGNED], s[8:]):
                return ('ok', kid.hex(), sv) if sv >= minsv else ('sig_old', None, sv)
    return ('sig_bad' if named else 'sig_key'), None, sv


def check_backup(pem, pub):
    text = b'TFN key check ' + os.urandom(16).hex().encode()
    rs = sign_raw(pem, text)
    return verify_raw(pub, text, rs) and not verify_raw(pub, text + b'.', rs)


# ---------------------------------------------------------------- the command line
def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('keygen')
    p.add_argument('out')
    p = sub.add_parser('pub')
    p.add_argument('pem')
    p = sub.add_parser('sign')
    p.add_argument('bin')
    p.add_argument('--key', action='append', required=True)
    p.add_argument('--kind')
    p.add_argument('--version')
    p.add_argument('--sec-ver', type=int, default=0)
    p.add_argument('--time', type=int)
    p.add_argument('--out')
    p = sub.add_parser('verify')
    p.add_argument('bin')
    p.add_argument('--pub', action='append', required=True)
    p.add_argument('--kind')
    p.add_argument('--minsv', type=int, default=0)
    p = sub.add_parser('check-backup')
    p.add_argument('pem')
    p.add_argument('--pub', required=True)
    a = ap.parse_args(argv)
    try:
        if a.cmd in ('keygen', 'pub'):
            pt = keygen(a.out) if a.cmd == 'keygen' else pub_of_pem(a.pem)
            print('#define TFN_OTA_PUBKEY<N>_BYTES %s' % c_bytes(pt))
            print('key ID %s' % key_id(pt).hex())
            return 0
        if a.cmd == 'sign':
            kind, ver = sign_file(a.bin, a.key, a.kind, a.version, a.sec_ver, a.time, a.out)
            print('signed %s: kind %s, version %s, sec_ver %d, %d signature(s)' % (a.out or a.bin, kind, ver,
                                                                                     a.sec_ver, len(a.key)))
            return 0
        if a.cmd == 'verify':
            with open(a.bin, 'rb') as h:
                data = h.read()
            body = data[:-TRAILER] if has_trailer(data) else data
            kind = a.kind or (image_kinds(body) or [''])[0]
            code, kid, sv = check(data, [parse_pub(x) for x in a.pub], kind, None, a.minsv)
            print('%s%s (kind %s, sec_ver %s)' % (code, ':' + kid if kid else '', kind, sv))
            return 0 if code == 'ok' else 1
        if a.cmd == 'check-backup':
            ok = check_backup(a.pem, parse_pub(a.pub))
            print('KEY CHECK %s' % ('OK' if ok else 'FAILED'))
            return 0 if ok else 1
    except (SignError, OSError) as e:
        print('fw_sign: %s' % e, file=sys.stderr)
        return 2
    return 2


if __name__ == '__main__':
    sys.exit(main())
