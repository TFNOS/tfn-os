#!/usr/bin/env python3
"""The test of sdk/tools/fw_sign.py and the vectors of the signed OTA that the C tests read (test_ota_sig.c,
test_ota_flow.c): the Python side signs, the C side verifies the same bytes.

It makes TEST keys in a temporary folder (they never go into git), small ESP-IDF images, and one signed file for each
case. Each case has the code that the board must give. fw_sign.check() must give the same code; where the Python
package "cryptography" exists, it also verifies the good signatures and refuses the bad ones (a second, independent
check). It also checks the commands of fw_sign.py, the SignError of a check when openssl does not run, that a public
key of another curve than P-256 is refused, that a P-256 key with explicit curve parameters gives the point and the
key ID of its named form (and explicit parameters with another value are refused), that keygen writes the named
curve, and for each kind of a DEV build folder that the length of tfn_os.bin is the image length of its ESP-IDF
header (the board finds the trailer there).

Usage: python tests/unit/test_ota_sig.py [--out <folder>]   (default: ota_vec in the temp folder of the system)
The C tests take the folder as their argument (tests/unit/run.py does both steps).
"""
import argparse
import contextlib
import hashlib
import io
import os
import random
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'sdk', 'tools'))
import fw_sign  # noqa: E402

KIND = 'lcd349'
VERSION = '1.0.97'
N = fw_sign.N
FAILS = []


def expect(cond, what):
    if not cond:
        FAILS.append(what)
        print('FAIL', what)


def make_image(kind=KIND, version=VERSION, seed=1, body=68000):
    """A small ESP-IDF image: the header, an app description segment (the version at 0x30), a data segment with the
    board tag, the checksum block and the appended SHA-256."""
    rnd = random.Random(seed)
    desc = bytearray(256)
    struct.pack_into('<I', desc, 0, 0xABCD5432)
    desc[0x10:0x30] = version.encode().ljust(32, b'\0')
    data = bytearray(rnd.getrandbits(8) for _ in range(body))
    for i, b in enumerate(data):
        if b == 0x40:
            data[i] = 0x41
    tag = b'@ARCADE-BOARD:' + kind.encode() + b'@'
    data[30000:30000 + len(tag)] = tag
    head = bytearray(24)
    head[0], head[1], head[2], head[3] = 0xE9, 2, 2, 0x2F
    struct.pack_into('<I', head, 4, 0x40380000)
    struct.pack_into('<H', head, 12, 9)
    head[23] = 1
    img = bytes(head) + struct.pack('<II', 0x3C000020, len(desc)) + bytes(desc)
    img += struct.pack('<II', 0x3C001000, len(data)) + bytes(data)
    pad = ((len(img) + 1 + 15) & ~15) - len(img)
    img += b'\0' * (pad - 1) + b'\xef'
    img += hashlib.sha256(img).digest()
    return img


def edit(data, off, fn):
    """A copy of data with the byte at off (from the end of the file when off < 0) changed by fn."""
    b = bytearray(data)
    b[off] = fn(b[off]) & 0xFF
    return bytes(b)


def set_rs(data, i, r=None, s=None):
    """A copy of the signed file with r and / or s of signature i set to the given numbers."""
    b = bytearray(data)
    o = len(b) - fw_sign.TRAILER + fw_sign.SIGNED + 72 * i + 8
    if r is not None:
        b[o:o + 32] = r.to_bytes(32, 'big')
    if s is not None:
        b[o + 32:o + 64] = s.to_bytes(32, 'big')
    return bytes(b)


def get_s(data, i):
    o = len(data) - fw_sign.TRAILER + fw_sign.SIGNED + 72 * i + 8
    return int.from_bytes(data[o + 32:o + 64], 'big')


def signed(img, keys, kind=KIND, version=VERSION, sv=0):
    return img + fw_sign.make_trailer(img, kind, version, sv, 1700000000, keys)


def make_cases(k1, k2, k3):
    img = make_image()
    good = signed(img, [k1])
    tr = len(good) - fw_sign.TRAILER
    two = signed(img, [k1, k2])
    other = make_image(kind='cores3', seed=2)
    cases = [
        ('good_k1', good, 0, 'ok'),
        ('good_k2', signed(img, [k2]), 0, 'ok'),
        ('two_keys', two, 0, 'ok'),
        ('two_first_bad', edit(two, tr + 128 + 8 + 5, lambda v: v ^ 0x01), 0, 'ok'),
        ('unknown_then_good', signed(img, [k3, k2]), 0, 'ok'),
        ('high_s', set_rs(good, 0, s=N - get_s(good, 0)), 0, 'ok'),
        ('sv1_minsv1', signed(img, [k1], sv=1), 1, 'ok'),
        ('bad_r', edit(good, tr + 128 + 8 + 3, lambda v: v ^ 0x80), 0, 'sig_bad'),
        ('bad_s', edit(good, tr + 128 + 40 + 31, lambda v: v ^ 0x01), 0, 'sig_bad'),
        ('r_zero', set_rs(good, 0, r=0), 0, 'sig_bad'),
        ('s_order', set_rs(good, 0, s=N), 0, 'sig_bad'),
        ('two_both_bad', edit(edit(two, tr + 128 + 40, lambda v: v ^ 4), tr + 200 + 40, lambda v: v ^ 4), 0,
         'sig_bad'),
        ('signed_field', edit(good, tr + 100, lambda v: v ^ 0x10), 0, 'sig_bad'),
        ('wrong_key', signed(img, [k3]), 0, 'sig_key'),
        ('wrong_kind', signed(other, [k1], kind='cores3'), 0, 'sig_kind'),
        ('wrong_version', signed(img, [k1], version='1.0.96'), 0, 'sig_kind'),
        ('old_sv', signed(img, [k1], sv=0), 1, 'sig_old'),
        ('image_byte', edit(good, 1000, lambda v: v ^ 0x01), 0, 'sig_hash'),
        ('nsig0', edit(good, tr + 108, lambda v: 0), 0, 'sig_form'),
        ('nsig5', edit(good, tr + 108, lambda v: 5), 0, 'sig_form'),
        ('format2', edit(good, tr + 8, lambda v: 2), 0, 'sig_form'),
        ('image_len', edit(good, tr + 12, lambda v: v + 1), 0, 'sig_form'),
        ('zero_area', edit(good, tr + 120, lambda v: 1), 0, 'sig_form'),
        ('cut', good[:-100], 0, 'sig_none'),
        ('unsigned', img, 0, 'sig_none'),
    ]
    return cases


def crypto_check(cases, pubs):
    """The good signatures verify with the package cryptography, the changed ones do not (when it exists)."""
    try:
        from cryptography.exceptions import InvalidSignature
        from cryptography.hazmat.primitives import hashes
        from cryptography.hazmat.primitives.asymmetric import ec
        from cryptography.hazmat.primitives.asymmetric.utils import encode_dss_signature
    except ImportError:
        print('(no package cryptography: the second check is not done)')
        return 0
    keys = {fw_sign.key_id(p): ec.EllipticCurvePublicKey.from_encoded_point(ec.SECP256R1(), p) for p in pubs}

    def ok(p, msg, rs):
        r, s = int.from_bytes(rs[:32], 'big'), int.from_bytes(rs[32:], 'big')
        if not (0 < r < N and 0 < s < N):
            return False
        try:
            keys[fw_sign.key_id(p)].verify(encode_dss_signature(r, s), msg, ec.ECDSA(hashes.SHA256()))
            return True
        except InvalidSignature:
            return False

    n = 0
    for name, data, minsv, want in cases:
        got = fw_sign.check(data, pubs, KIND, None, minsv, verify=ok)[0]
        expect(got == want, 'cryptography %s: %s, not %s' % (name, got, want))
        n += 1
    return n


def build_dev_lengths():
    """The length of tfn_os.bin = the image length of its header, for each kind of a DEV build folder."""
    roots = [os.environ.get('TFN_BUILD_DEV', ''), os.path.join(ROOT, 'build_dev')]  # (TFN_BUILD_DEV: another folder)
    for root in roots:
        if not root or not os.path.isdir(root):
            continue
        kinds = []
        for kind in sorted(os.listdir(root)):
            p = os.path.join(root, kind, 'tfn_os.bin')
            if not os.path.isfile(p):
                continue
            with open(p, 'rb') as h:
                data = h.read()
            n = fw_sign.image_len(data)
            expect(n == len(data), 'build_dev %s: the file has %d bytes, the image %d' % (kind, len(data), n))
            expect(fw_sign.image_kinds(data) == [kind], 'build_dev %s: board tags %s' % (kind,
                                                                                          fw_sign.image_kinds(data)))
            kinds.append(kind)
        if kinds:
            print('build_dev image lengths OK: %s (%s)' % (', '.join(kinds), os.path.normpath(root)))
            return len(kinds)
    print('(no DEV build folder: the image length check of the real images is not done)')
    return 0


def pub_file_checks(d, p, k1, pubs):
    """verify --pub with a file: the content tells the form (a PEM header, else the hex text)."""
    forms = [
        ('hex.pub', (pubs[0].hex() + '\n').encode(), 0, 'a file with the hex text (and a line end)'),
        ('upper.txt', (pubs[1].hex().upper() + '\r\n').encode(), 0, 'a file with the hex text in capitals (CRLF)'),
        ('cbytes.txt', fw_sign.c_bytes(pubs[0]).encode(), 0, 'a file with the C bytes "0x04, 0x.."'),
        ('pub1.pem', fw_sign.run_ssl(['pkey', '-in', k1, '-pubout']), 0, 'a public PEM file'),
        ('other.pub', (pubs[2].hex() + '\n').encode(), 1, 'a file with the hex text of a key that did not sign'),
        ('short.pub', pubs[0].hex()[:64].encode(), 2, 'a file with too few hex digits'),
        ('bin.pub', b'\x30\x82\xff\x00', 2, 'a binary file'),
    ]
    for name, data, code, what in forms:
        f = os.path.join(d, name)
        with open(f, 'wb') as h:
            h.write(data)
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            got = fw_sign.main(['verify', p, '--pub', f])
        expect(got == code, 'verify --pub %s: exit %d, not %d' % (what, got, code))
        expect(pubs[0].hex()[:16] not in err.getvalue() and pubs[2].hex()[:16] not in err.getvalue(),
               'verify --pub %s: the message shows no key text' % what)
    err = io.StringIO()
    with contextlib.redirect_stderr(err):
        got = fw_sign.main(['verify', p, '--pub', os.path.join(d, 'no_such.pub')])
    expect(got == 2 and 'no file' in err.getvalue(), 'verify --pub with a file that is not there: exit 2, "no file"')
    for got, want in ((fw_sign.parse_pub(os.path.join(d, 'hex.pub')), pubs[0]),
                      (fw_sign.parse_pub(os.path.join(d, 'pub1.pem')), pubs[0]), (fw_sign.parse_pub(k1), pubs[0])):
        expect(got == want, 'parse_pub: a file gives the same point as the private PEM')


def curve_checks(d, pubs):
    """point_from_spki takes only a P-256 key: a SubjectPublicKeyInfo of another curve also ends with a 65-byte point
    (secp256k1, brainpoolP256r1), and fw_sign.py refuses it (parse_pub of a PEM file of such a key too)."""
    pt = pubs[0]
    expect(fw_sign.point_from_spki(fw_sign.spki_from_point(pt)) == pt, 'point_from_spki of a P-256 key')
    ec_key = bytes.fromhex('06072a8648ce3d0201')
    others = {'secp256k1': bytes.fromhex('06052b8104000a'), 'brainpoolP256r1': bytes.fromhex('06092b2403030208010107')}
    for name, oid in others.items():
        alg = b'\x30' + bytes([len(ec_key) + len(oid)]) + ec_key + oid
        body = alg + b'\x03\x42\x00' + pt
        expect(spki_refused(b'\x30' + bytes([len(body)]) + body), 'point_from_spki: a %s key is refused' % name)
    expect(spki_refused(fw_sign.spki_from_point(pt)[:-1]), 'point_from_spki: a short key is refused')
    expect(spki_refused(fw_sign.spki_from_point(b'\x02' + pt[1:])), 'point_from_spki: no 04 form: refused')
    pem = os.path.join(d, 'k256.pem')
    try:
        with open(pem, 'wb') as h:
            h.write(fw_sign.run_ssl(['genpkey', '-algorithm', 'EC', '-pkeyopt', 'ec_paramgen_curve:secp256k1']))
    except fw_sign.SignError as e:
        print('(no secp256k1 key of this openssl: %s)' % str(e)[:80])
        return
    try:
        fw_sign.parse_pub(pem)
        refused = False
    except fw_sign.SignError:
        refused = True
    expect(refused, 'parse_pub of a PEM file of a secp256k1 key: refused')


def spki_refused(der):
    try:
        fw_sign.point_from_spki(der)
    except fw_sign.SignError:
        return True
    return False


# the values of P-256 for the test (SEC 2 secp256r1); explicit_spki_checks compares its ECParameters with those of
# openssl
T_P = 0xFFFFFFFF00000001000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFF
T_B = 0x5AC635D8AA3A93E7B3EBBD55769886BC651D06B0CC53B0F63BCE3C3E27D2604B
T_G = bytes.fromhex('046B17D1F2E12C4247F8BCE6E563A440F277037D812DEB33A0F4A13945D898C296'
                    '4FE342E2FE1A7F9B8EE7EB4A7C0F9E162BCE33576B315ECECBB6406837BF51F5')
T_SEED = bytes.fromhex('c49d360886e704936a6678e1139d26b7819f7e90')
T_PRIME_FIELD = bytes.fromhex('2a8648ce3d0101')
T_EC_KEY = bytes.fromhex('2a8648ce3d0201')  # id-ecPublicKey
T_NAMED = bytes.fromhex('06082a8648ce3d030107')  # the parameters "the named curve prime256v1"
T_SPKI_NAMED = bytes.fromhex('3059301306072a8648ce3d020106082a8648ce3d030107034200')


def named_form(der_key):
    """True: der_key is the short P-256 SubjectPublicKeyInfo (the named curve, 91 bytes)."""
    return len(der_key) == len(T_SPKI_NAMED) + 65 and der_key.startswith(T_SPKI_NAMED)


def der(tag, body):
    n = len(body)
    ln = bytes([n]) if n < 0x80 else b'\x81' + bytes([n]) if n < 0x100 else b'\x82' + n.to_bytes(2, 'big')
    return bytes([tag]) + ln + body


def der_int(v):
    return der(0x02, v.to_bytes(v.bit_length() // 8 + 1, 'big'))


def ec_params(p=T_P, a=T_P - 3, b=T_B, seed=T_SEED, g=T_G, n=N, h=1, ver=1, field=T_PRIME_FIELD,
              extra=(b'', b'', b'')):
    """The DER of explicit ECParameters (SEC 1, C.2). seed None, h None: that optional part is not there. extra: more
    bytes at the end of the field ID, of the curve and of the parameters."""
    curve = der(0x04, a.to_bytes(32, 'big')) + der(0x04, b.to_bytes(32, 'big'))
    if seed is not None:
        curve += der(0x03, b'\x00' + seed)
    body = (der_int(ver) + der(0x30, der(0x06, field) + der_int(p) + extra[0]) + der(0x30, curve + extra[1]) +
            der(0x04, g) + der_int(n))
    if h is not None:
        body += der_int(h)
    return der(0x30, body + extra[2])


def spki_of(params, pt, bits=None, oid=T_EC_KEY, extra=b''):
    """A SubjectPublicKeyInfo of the algorithm oid (id-ecPublicKey) with these parameters (DER) and the point pt.
    extra: more bytes after the BIT STRING."""
    alg = der(0x30, der(0x06, oid) + params)
    return der(0x30, alg + der(0x03, b'\x00' + pt if bits is None else bits) + extra)


def ssl_or_none(*cmds):
    """The output of the first openssl command that works, or None (an openssl with no such option or curve)."""
    for c in cmds:
        try:
            return fw_sign.run_ssl(c)
        except fw_sign.SignError:
            pass
    return None


def explicit_spki_checks(pt):
    """point_from_spki takes explicit parameters only with the values of P-256: each other value is refused."""
    ref = ssl_or_none(['ecparam', '-name', 'prime256v1', '-param_enc', 'explicit', '-outform', 'DER'])
    expect(ref is None or ref in (ec_params(), ec_params(seed=None)),
           'the explicit P-256 parameters of this test are those of openssl')
    expect(spki_of(T_NAMED, pt) == fw_sign.spki_from_point(pt), 'the named form of this test')
    for what, params in (('the seed and the cofactor', ec_params()), ('no seed', ec_params(seed=None)),
                         ('no cofactor', ec_params(h=None)), ('no seed, no cofactor', ec_params(seed=None, h=None))):
        try:
            got = fw_sign.point_from_spki(spki_of(params, pt))
        except fw_sign.SignError:
            got = None
        expect(got == pt, 'point_from_spki: explicit P-256 parameters (%s): the point' % what)
    flip = bytes([T_SEED[0] ^ 1]) + T_SEED[1:]
    bad = [('another p', ec_params(p=T_P + 2)), ('another a', ec_params(a=T_P - 4)),
           ('another b', ec_params(b=T_B ^ 1)), ('another seed', ec_params(seed=flip)),
           ('another G', ec_params(g=T_G[:-1] + bytes([T_G[-1] ^ 1]))),
           ('a compressed G', ec_params(g=b'\x03' + T_G[1:33])), ('another order', ec_params(n=N ^ 2)),
           ('the cofactor 2', ec_params(h=2)), ('the cofactor 0', ec_params(h=0)), ('version 2', ec_params(ver=2)),
           ('the field characteristic-two', ec_params(field=bytes.fromhex('2a8648ce3d0102'))),
           ('one more part of the field', ec_params(extra=(b'\x05\x00', b'', b''))),
           ('one more part of the curve', ec_params(extra=(b'', b'\x05\x00', b''))),
           ('one more part', ec_params(extra=(b'', b'', b'\x05\x00'))),
           ('a part after the parameters', ec_params() + b'\x05\x00')]
    for name in ('secp256k1', 'brainpoolP256r1'):
        other = ssl_or_none(['ecparam', '-name', name, '-param_enc', 'explicit', '-outform', 'DER'])
        if other is None:
            print('(no explicit %s parameters of this openssl)' % name)
        else:
            bad.append(('the explicit parameters of %s' % name, other))
    for what, params in bad:
        expect(spki_refused(spki_of(params, pt)), 'point_from_spki: explicit parameters with %s: refused' % what)
    for params, form in ((ec_params(), 'explicit'), (T_NAMED, 'named')):
        expect(spki_refused(spki_of(params, pt) + b'\x00'),
               'point_from_spki (%s): a byte after the key: refused' % form)
        expect(spki_refused(spki_of(params, pt, extra=b'\x05\x00')),
               'point_from_spki (%s): a part after the BIT STRING: refused' % form)
        expect(spki_refused(spki_of(params, pt, oid=bytes.fromhex('2b8104010c'))),
               'point_from_spki (%s): the algorithm id-ecDH: refused' % form)
        expect(spki_refused(spki_of(params, b'\x02' + pt[1:33])), 'point_from_spki (%s): a compressed point: refused'
               % form)
        expect(spki_refused(spki_of(params, pt + b'\x00')),
               'point_from_spki (%s): a point of 66 bytes: refused' % form)
        expect(spki_refused(spki_of(params, pt[:-1])), 'point_from_spki (%s): a point of 64 bytes: refused' % form)
        expect(spki_refused(spki_of(params, pt, b'\x01' + pt)), 'point_from_spki (%s): a BIT STRING of 7 bits: '
               'refused' % form)


def explicit_key_checks(d, img):
    """A P-256 key with explicit parameters (as genpkey of LibreSSL writes it) gives the same point and key ID as its
    named form, and it signs; a secp256k1 key with explicit parameters is refused."""
    pem = os.path.join(d, 'explicit.pem')
    key = ssl_or_none(['genpkey', '-algorithm', 'EC', '-pkeyopt', 'ec_paramgen_curve:P-256', '-pkeyopt',
                       'ec_param_enc:explicit'], ['ecparam', '-name', 'prime256v1', '-param_enc', 'explicit',
                                                  '-genkey', '-noout'])
    if key is None:
        print('(no key with explicit parameters from this openssl: the check of such a key is not done)')
        return None
    with open(pem, 'wb') as h:
        h.write(key)
    der_exp = fw_sign.run_ssl(['pkey', '-in', pem, '-pubout', '-outform', 'DER'])
    named = ssl_or_none(['ec', '-in', pem, '-pubout', '-outform', 'DER', '-param_enc', 'named_curve'])
    expect(not named_form(der_exp) and len(der_exp) > 300, 'the TEST key has explicit parameters')
    if named is None or not named_form(named):
        print('(this openssl does not write the named form of the key: the point of the check is the end of the key)')
        named = None
    want = named[-65:] if named else der_exp[-65:]
    pub_pem = os.path.join(d, 'explicit_pub.pem')
    with open(pub_pem, 'wb') as h:
        h.write(fw_sign.run_ssl(['pkey', '-in', pem, '-pubout']))
    for what, path in (('the private PEM', pem), ('the public PEM', pub_pem)):
        try:
            got = fw_sign.parse_pub(path)
        except fw_sign.SignError as e:
            got = 'refused: %s' % e
        expect(got == want, 'a P-256 key with explicit parameters (%s): the point of the named form (%s)'
               % (what, got if isinstance(got, str) else 'other point'))
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        code = fw_sign.main(['pub', pem])
    expect(code == 0 and 'key ID %s' % fw_sign.key_id(want).hex() in out.getvalue(),
           'pub of a key with explicit parameters: the key ID of the named form')
    p = os.path.join(d, 'img_explicit.bin')
    with open(p, 'wb') as h:
        h.write(img)
    expect(fw_sign.main(['sign', p, '--key', pem, '--time', '5']) == 0, 'sign with a key with explicit parameters')
    expect(fw_sign.main(['verify', p, '--pub', want.hex()]) == 0, 'verify (the named point) of that signature')
    expect(fw_sign.main(['check-backup', pem, '--pub', want.hex()]) == 0, 'check-backup of a key with explicit '
           'parameters')
    k1 = os.path.join(d, 'explicit_k1.pem')
    other = ssl_or_none(['genpkey', '-algorithm', 'EC', '-pkeyopt', 'ec_paramgen_curve:secp256k1', '-pkeyopt',
                         'ec_param_enc:explicit'], ['ecparam', '-name', 'secp256k1', '-param_enc', 'explicit',
                                                    '-genkey', '-noout'])
    if other is None:
        print('(no secp256k1 key with explicit parameters from this openssl)')
        return key
    with open(k1, 'wb') as h:
        h.write(other)
    try:
        fw_sign.parse_pub(k1)
        refused = False
    except fw_sign.SignError:
        refused = True
    expect(refused, 'parse_pub of a secp256k1 key with explicit parameters: refused')
    return key


def keygen_checks(d, k1, explicit):
    """keygen writes the named form. An openssl that writes explicit parameters in spite of the option stops keygen
    (no file); an openssl with no ec_param_enc in genpkey gets the command ecparam."""
    der_k1 = fw_sign.run_ssl(['pkey', '-in', k1, '-pubout', '-outform', 'DER'])
    expect(named_form(der_k1), 'keygen: the key has the named curve prime256v1')
    real = fw_sign.run_ssl
    used = []

    def no_genpkey(args, data=None):
        used.append(args[0])
        if args[0] == 'genpkey':
            raise fw_sign.SignError('openssl genpkey: parameter setting error')
        return real(args, data)

    def explicit_only(args, data=None):
        return explicit if args[0] in ('genpkey', 'ecparam') else real(args, data)

    try:
        fw_sign.run_ssl = no_genpkey
        k = os.path.join(d, 'kg_ecparam.pem')
        pt = fw_sign.keygen(k)
        expect('ecparam' in used and len(pt) == 65 and named_form(
            real(['pkey', '-in', k, '-pubout', '-outform', 'DER'])), 'keygen with no genpkey: ecparam, the named curve')
        if explicit:
            fw_sign.run_ssl = explicit_only
            k = os.path.join(d, 'kg_explicit.pem')
            try:
                fw_sign.keygen(k)
                stop = ''
            except fw_sign.SignError as e:
                stop = str(e)
            expect('explicit' in stop and not os.path.exists(k), 'keygen: a key with explicit parameters stops, no '
                   'file (%s)' % stop)
    finally:
        fw_sign.run_ssl = real


def verify_error(pub):
    """The type of the error of verify_raw for a signature in the range ('' for no error)."""
    try:
        fw_sign.verify_raw(pub, b'TFN OS', bytes(range(1, 65)))
        return ''
    except Exception as e:  # noqa: BLE001  (the test tells the type of each error)
        return type(e).__name__


def verify_fault_checks(pub):
    """verify_raw gives a SignError (not an OSError or a TimeoutExpired) when the openssl program does not start or
    does not end in time."""
    old_env, old_run = os.environ.get('OPENSSL'), subprocess.run

    def late(*args, **kw):
        raise subprocess.TimeoutExpired(args[0] if args else 'openssl', 60)
    try:
        os.environ['OPENSSL'] = os.path.join(tempfile.gettempdir(), 'tfn_no_such_dir', 'openssl.exe')
        got = verify_error(pub)
        expect(got == 'SignError', 'verify_raw, no openssl program: a SignError (%s)' % got)
        if old_env is None:
            os.environ.pop('OPENSSL', None)
        else:
            os.environ['OPENSSL'] = old_env
        subprocess.run = late
        got = verify_error(pub)
        expect(got == 'SignError', 'verify_raw, openssl does not end in time: a SignError (%s)' % got)
    finally:
        subprocess.run = old_run
        if old_env is None:
            os.environ.pop('OPENSSL', None)
        else:
            os.environ['OPENSSL'] = old_env


def tool_checks(d, k1, k2, pubs):
    """The commands of fw_sign.py."""
    img = make_image(seed=5)
    p = os.path.join(d, 'img.bin')
    with open(p, 'wb') as h:
        h.write(img)
    expect(fw_sign.main(['sign', p, '--key', k1, '--key', k2, '--sec-ver', '3', '--time', '5']) == 0, 'sign')
    with open(p, 'rb') as h:
        data = h.read()
    expect(len(data) == len(img) + fw_sign.TRAILER and data[:len(img)] == img, 'sign: the image stays the same')
    expect(fw_sign.check(data, pubs, KIND, None, 3)[0] == 'ok', 'sign: the check')
    expect(fw_sign.main(['verify', p, '--pub', k1]) == 0, 'verify with the PEM of key 1')
    expect(fw_sign.main(['verify', p, '--pub', pubs[1].hex()]) == 0, 'verify with the hex of key 2')
    expect(fw_sign.main(['verify', p, '--pub', pubs[2].hex()]) == 1, 'verify with a key that did not sign')
    expect(fw_sign.main(['verify', p, '--pub', k1, '--minsv', '4']) == 1, 'verify: minsv 4 > sec_ver 3')
    pub_file_checks(d, p, k1, pubs)
    expect(fw_sign.main(['sign', p, '--key', k1]) == 2, 'sign: a file with a trailer')
    q = os.path.join(d, 'img2.bin')
    with open(q, 'wb') as h:
        h.write(img[:-10])
    expect(fw_sign.main(['sign', q, '--key', k1]) == 2, 'sign: a cut image')
    with open(q, 'wb') as h:
        h.write(img)
    expect(fw_sign.main(['sign', q, '--key', k1, '--kind', 'cores3']) == 2, 'sign: a kind with no tag')
    expect(fw_sign.main(['sign', q, '--key', k1, '--version', '9.9.9']) == 2, 'sign: another version')
    expect(fw_sign.main(['keygen', k1]) == 2, 'keygen: an existing file stays')
    expect(fw_sign.main(['check-backup', k1, '--pub', k1]) == 0, 'check-backup: the same key')
    expect(fw_sign.main(['check-backup', k1, '--pub', pubs[1].hex()]) == 1, 'check-backup: another key')
    rs = bytes(range(1, 65))
    expect(fw_sign.der_to_rs(fw_sign.rs_to_der(rs)) == rs, 'DER round trip')
    hi = b'\x80' + bytes(31) + b'\x00' * 31 + b'\x01'
    expect(fw_sign.der_to_rs(fw_sign.rs_to_der(hi)) == hi, 'DER round trip (a high bit, a short s)')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(tempfile.gettempdir(), 'tfn_ota_vec'))
    a = ap.parse_args()
    try:
        fw_sign.openssl()
    except fw_sign.SignError as e:
        print('OTA SIG PY FAILED: %s' % e)
        return 1
    d = tempfile.mkdtemp(prefix='tfn_ota_keys_')
    try:
        k1, k2, k3 = (os.path.join(d, 'test_key%d.pem' % i) for i in (1, 2, 3))
        pubs = [fw_sign.keygen(k) for k in (k1, k2, k3)]
        cases = make_cases(k1, k2, k3)
        os.makedirs(a.out, exist_ok=True)
        with open(os.path.join(a.out, 'keys.txt'), 'w', newline='\n') as h:
            h.write('# the key list of the board (test keys of this run): the board trusts key 1 and key 2\n')
            h.write('%s\n%s\n-\n-\n' % (pubs[0].hex(), pubs[1].hex()))
        with open(os.path.join(a.out, 'cases.txt'), 'w', newline='\n') as h:
            h.write('# <file> <board kind> <minsv> <the code of the board>\n')
            for name, data, minsv, want in cases:
                with open(os.path.join(a.out, name + '.bin'), 'wb') as b:
                    b.write(data)
                h.write('%s.bin %s %d %s\n' % (name, KIND, minsv, want))
        board = [pubs[0], pubs[1]]
        for name, data, minsv, want in cases:
            got = fw_sign.check(data, board, KIND, None, minsv)[0]
            expect(got == want, 'fw_sign.check %s: %s, not %s' % (name, got, want))
        nc = crypto_check(cases, board)
        tool_checks(d, k1, k2, pubs)
        verify_fault_checks(pubs[0])
        curve_checks(d, pubs)
        explicit_spki_checks(pubs[0])
        keygen_checks(d, k1, explicit_key_checks(d, make_image(seed=6)))
        nb = build_dev_lengths()
    finally:
        shutil.rmtree(d, ignore_errors=True)
    print('OTA SIG PY %s (%d vectors in %s, %d checked with cryptography, %d real images)' % (
        'OK' if not FAILS else '%d FAILED' % len(FAILS), len(cases), os.path.relpath(a.out, ROOT), nc, nb))
    return 1 if FAILS else 0


if __name__ == '__main__':
    sys.exit(main())
