#!/usr/bin/env python3
"""The test of sdk/tools/pool_crypto.py and sdk/tools/pool.py, and the shared vectors of the pools:
tests/unit/vectors/pool_v1.json. The C core of the board (main/tfn_pool_core.c, tests/unit/test_pool_core.c) reads
the same file: the two sides must give the same bytes.

1. The published vectors: RFC 7748 (X25519), RFC 8439 (ChaCha20, Poly1305, the AEAD), RFC 5869 (HKDF-SHA256) and the
   SipHash-2-4 vectors of its authors (key 00..0f, the messages 00..n-1; the openssl program checks all 64 and the
   two values of the paper are in this file).
2. A second, independent check where it exists: the Python package "cryptography" (X25519, ChaCha20-Poly1305, HKDF,
   P-256) with random inputs.
3. The pool vectors: the roster files (and bad ones), the rules of the roster, the keys made from the pool key, link
   v4 frames, the replay window, the UDP packets of version 2, the beacons, the SAS, a full JOIN, the envelope of
   op=key, the request header. All inputs come from fixed labels (TEST values: no real key), and the ECDSA
   signatures are deterministic (RFC 6979, a small P-256 code in this file), so the file is the same at each run.
   openssl verifies each signature.

Usage: python tests/unit/test_pool_crypto.py            make the vectors and compare them with the file
       python tests/unit/test_pool_crypto.py --update   write the file (after a change of a format: the C side must
                                                        agree; tell its owner)
"""
import argparse
import hashlib
import hmac
import json
import os
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'sdk', 'tools'))
import fw_sign  # noqa: E402
import pool  # noqa: E402
import pool_crypto as pc  # noqa: E402

VEC_FILE = os.path.join(HERE, 'vectors', 'pool_v1.json')
FAILS = []
h = bytes.fromhex


def expect(cond, what):
    if not cond:
        FAILS.append(what)
        print('FAIL', what)


# ---------------------------------------------------------------- the published vectors
X25519_RFC = [
    ('a546e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4',
     'e6db6867583030db3594c1a424b15f7c726624ec26b3353b10a903a6d0ab1c4c',
     'c3da55379de9c6908e94ea4df28d084f32eccf03491c71f754b4075577a28552'),
    ('4b66e9d4d1b4673c5ad22691957d6af5c11b6421e0ea01d42ca4169e7918ba0d',
     'e5210f12786811d3f4b7959d0538ae2c31dbe7106fc03c3efc4cd549c715a493',
     '95cbde9476e8907d7aade45cb4b873f88b595a68799fa152e6f8f7647aac7957'),
]
X25519_ITER = [(1, '422c8e7a6227d7bca1350b3e2bb7279f7897b87bb6854b783c60e80311ae3079'),
               (1000, '684cf59ba83309552800ef566f2f4d3c1c3887c49360e3875f2eb94d99532c51')]
X25519_DH = {'alice_priv': '77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a',
             'alice_pub': '8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a',
             'bob_priv': '5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb',
             'bob_pub': 'de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f',
             'shared': '4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742'}
SUNSCREEN = (b"Ladies and Gentlemen of the class of '99: If I could offer you only one tip for the future, "
             b"sunscreen would be it.")
CHACHA_BLOCK = {'k': bytes(range(32)).hex(), 'counter': 1, 'nonce': '000000090000004a00000000',
                'out': '10f1e7e4d13b5915500fdd1fa32071c4c7d1f4c733c068030422aa9ac3d46c4e'
                       'd2826446079faa0914c2d705d98b02a2b5129cd1de164eb9cbd083e8a2503c4e'}
CHACHA_ENC = {'k': bytes(range(32)).hex(), 'counter': 1, 'nonce': '000000000000004a00000000',
              'plain': SUNSCREEN.hex(),
              'out': '6e2e359a2568f98041ba0728dd0d6981e97e7aec1d4360c20a27afccfd9fae0bf91b65c5524733ab8f593dabcd62b357'
                     '1639d624e65152ab8f530c359f0861d807ca0dbf500d6a6156a38e088a22b65e52bc514d16ccf806818ce91ab779'
                     '37365af90bbf74a35be6b40b8eedf2785e42874d'}
POLY = {'k': '85d6be7857556d337f4452fe42d506a80103808afb0db2fd4abff6af4149f51b',
        'msg': b'Cryptographic Forum Research Group'.hex(), 'tag': 'a8061dc1305136c6c22b8baf0c0127a9'}
AEAD = {'k': bytes(range(0x80, 0xa0)).hex(), 'nonce': '070000004041424344454647', 'aad': '50515253c0c1c2c3c4c5c6c7',
        'plain': SUNSCREEN.hex(),
        'ct': 'd31a8d34648e60db7b86afbc53ef7ec2a4aded51296e08fea9e2b5a736ee62d63dbea45e8ca9671282fafb69da92728b'
              '1a71de0a9e060b2905d6a5b67ecd3b3692ddbd7f2d778b8c9803aee328091b58fab324e4fad675945585808b4831d7bc'
              '3ff4def08e4b7a9de576d26586cec64b6116',
        'tag': '1ae10b594f09e26a7e902ecbd0600691'}
HKDF_RFC = [
    {'ikm': '0b' * 22, 'salt': bytes(range(13)).hex(), 'info': bytes(range(0xf0, 0xfa)).hex(), 'n': 42,
     'prk': '077709362c2e32df0ddc3f0dc47bba6390b6c73bb50f9c3122ec844ad7c2b3e5',
     'okm': '3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865'},
    {'ikm': bytes(range(0x50)).hex(), 'salt': bytes(range(0x60, 0xb0)).hex(), 'info': bytes(range(0xb0, 0x100)).hex(),
     'n': 82, 'prk': '06a6b88c5853361a06104c9ceb35b45cef760014904671014a193f40c15fc244',
     'okm': 'b11e398dc80327a1c8e7f78c596a49344f012eda2d4efad8a050cc4c19afa97c59045a99cac7827271cb41c65e590e09'
            'da3275600c2f09b8367793a9aca3db71cc30c58179ec3e87c14c01d5c1f3434f1d87'},
    {'ikm': '0b' * 22, 'salt': '', 'info': '', 'n': 42,
     'prk': '19ef24a32c717b167f33a91d6f648bdf96596776afdb6377ac434c1c293ccb04',
     'okm': '8da4e775a563c18f715f802a063c5a31b8a11f5c5ee1879ec3454e5f3c738d2d9d201395faa4b61a96c8'},
]
# the paper of SipHash (appendix A: the 15-byte message) and the first line of the vectors of its authors
SIPHASH_PAPER = [(15, 'e545be4961ca29a1'), (0, '310e0edd47db6f72')]


def check_published():
    for k, u, out in X25519_RFC:
        expect(pc.x25519(h(k), h(u)).hex() == out, 'X25519 RFC 7748 5.2 %s' % out[:8])
    k = u = pc.X25519_BASE
    done = 0
    for n, want in X25519_ITER:
        while done < n:
            k, u = pc.x25519(k, u), k
            done += 1
        expect(k.hex() == want, 'X25519 RFC 7748 5.2 after %d' % n)
    d = X25519_DH
    expect(pc.x25519_pub(h(d['alice_priv'])).hex() == d['alice_pub'], 'X25519 RFC 7748 6.1 Alice')
    expect(pc.x25519_pub(h(d['bob_priv'])).hex() == d['bob_pub'], 'X25519 RFC 7748 6.1 Bob')
    expect(pc.x25519_shared(h(d['alice_priv']), h(d['bob_pub'])).hex() == d['shared'], 'X25519 6.1 shared A')
    expect(pc.x25519_shared(h(d['bob_priv']), h(d['alice_pub'])).hex() == d['shared'], 'X25519 6.1 shared B')
    try:
        pc.x25519_shared(h(d['alice_priv']), bytes(32))
        expect(False, 'X25519: the zero point is refused')
    except pc.CryptoError:
        pass
    b = CHACHA_BLOCK
    expect(pc.chacha20_block(h(b['k']), b['counter'], h(b['nonce'])).hex() == b['out'], 'ChaCha20 RFC 8439 2.3.2')
    e = CHACHA_ENC
    expect(pc.chacha20_xor(h(e['k']), e['counter'], h(e['nonce']), h(e['plain'])).hex() == e['out'],
           'ChaCha20 RFC 8439 2.4.2')
    expect(pc.poly1305(h(POLY['k']), h(POLY['msg'])).hex() == POLY['tag'], 'Poly1305 RFC 8439 2.5.2')
    a = AEAD
    s = pc.aead_seal(h(a['k']), h(a['nonce']), h(a['plain']), h(a['aad']))
    expect(s.hex() == a['ct'] + a['tag'], 'AEAD RFC 8439 2.8.2')
    expect(pc.aead_open(h(a['k']), h(a['nonce']), s, h(a['aad'])) == h(a['plain']), 'AEAD open')
    for i in (0, len(s) - 1, len(s) - 17):
        bad = bytearray(s)
        bad[i] ^= 1
        expect(pc.aead_open(h(a['k']), h(a['nonce']), bytes(bad), h(a['aad'])) is None, 'AEAD: byte %d changed' % i)
    expect(pc.aead_open(h(a['k']), h(a['nonce']), s, b'') is None, 'AEAD: another aad')
    for i, v in enumerate(HKDF_RFC):
        prk = pc.hkdf_extract(h(v['salt']), h(v['ikm']))
        expect(prk.hex() == v['prk'], 'HKDF RFC 5869 case %d PRK' % (i + 1))
        expect(pc.hkdf(h(v['ikm']), h(v['salt']), h(v['info']), v['n']).hex() == v['okm'],
               'HKDF RFC 5869 case %d OKM' % (i + 1))
    for n, want in SIPHASH_PAPER:
        expect(pc.siphash24(bytes(range(16)), bytes(range(n))).hex() == want, 'SipHash paper %d bytes' % n)


def siphash_vectors():
    return [pc.siphash24(bytes(range(16)), bytes(range(n))).hex() for n in range(64)]


def siphash_openssl(outs, d):
    """The 64 SipHash values against the SIPHASH MAC of the openssl program (an independent code)."""
    try:
        exe = fw_sign.openssl()
    except fw_sign.SignError:
        print('(no openssl: the SipHash check of openssl is not done)')
        return 0
    n = 0
    for i, want in enumerate(outs):
        p = os.path.join(d, 'sip.bin')
        with open(p, 'wb') as f:
            f.write(bytes(range(i)))
        r = subprocess.run([exe, 'mac', '-macopt', 'hexkey:' + bytes(range(16)).hex(), '-macopt', 'size:8', '-in', p,
                            'SIPHASH'], capture_output=True, text=True)
        # LibreSSL (the openssl of macOS) has no "mac" command: it gives exit 0 and no output
        if r.returncode or not r.stdout.strip():
            print('(openssl has no SIPHASH MAC: the check is not done)')
            return 0
        expect(r.stdout.strip().lower() == want, 'SipHash %d bytes: openssl %s' % (i, r.stdout.strip()))
        n += 1
    return n


def check_cryptography():
    """Random inputs: pool_crypto against the package cryptography (when it exists)."""
    try:
        from cryptography.hazmat.primitives import hashes
        from cryptography.hazmat.primitives.asymmetric import ec
        from cryptography.hazmat.primitives.asymmetric.x25519 import X25519PrivateKey, X25519PublicKey
        from cryptography.hazmat.primitives.ciphers.aead import ChaCha20Poly1305
        from cryptography.hazmat.primitives.kdf.hkdf import HKDF
    except ImportError:
        print('(no package cryptography: the second check is not done)')
        return 0
    n = 0
    for i in range(40):
        k, nonce, plain, aad = os.urandom(32), os.urandom(12), os.urandom(i * 9), os.urandom(i % 23)
        expect(ChaCha20Poly1305(k).encrypt(nonce, plain, aad) == pc.aead_seal(k, nonce, plain, aad), 'AEAD %d' % i)
        a, b = os.urandom(32), os.urandom(32)
        ref = X25519PrivateKey.from_private_bytes(a).exchange(X25519PublicKey.from_public_bytes(pc.x25519_pub(b)))
        expect(ref == pc.x25519(a, pc.x25519_pub(b)), 'X25519 %d' % i)
        ikm, salt, info = os.urandom(32), os.urandom(i % 33), os.urandom(i % 11)
        ref = HKDF(hashes.SHA256(), 32 + i, salt or None, info).derive(ikm)
        expect(ref == pc.hkdf(ikm, salt, info, 32 + i), 'HKDF %d' % i)
        n += 3
    for label in (b'one', b'two'):
        d = P256.scalar(label)
        ref = ec.derive_private_key(d, ec.SECP256R1()).public_key().public_numbers()
        expect(P256.pub(d) == b'\x04' + ref.x.to_bytes(32, 'big') + ref.y.to_bytes(32, 'big'), 'P-256 public key')
        n += 1
    return n


# ---------------------------------------------------------------- a deterministic P-256 (only for the vectors)
class P256:
    """ECDSA P-256 / SHA-256 with the nonce of RFC 6979. Only to make the same vector file at each run: the tools
    use the openssl program."""
    p = 2 ** 256 - 2 ** 224 + 2 ** 192 + 2 ** 96 - 1
    n = fw_sign.N
    b = 0x5ac635d8aa3a93e7b3ebbd55769886bc651d06b0cc53b0f63bce3c3e27d2604b
    G = (0x6b17d1f2e12c4247f8bce6e563a440f277037d812deb33a0f4a13945d898c296,
         0x4fe342e2fe1a7f9b8ee7eb4a7c0f9e162bce33576b315ececbb6406837bf51f5)

    @classmethod
    def add(cls, P, Q):
        p = cls.p
        if P is None:
            return Q
        if Q is None:
            return P
        if P[0] == Q[0] and (P[1] + Q[1]) % p == 0:
            return None
        if P == Q:
            m = (3 * P[0] * P[0] - 3) * pow(2 * P[1], p - 2, p) % p
        else:
            m = (Q[1] - P[1]) * pow(Q[0] - P[0], p - 2, p) % p
        x = (m * m - P[0] - Q[0]) % p
        return x, (m * (P[0] - x) - P[1]) % p

    @classmethod
    def mul(cls, k, P):
        R = None
        while k:
            if k & 1:
                R = cls.add(R, P)
            P = cls.add(P, P)
            k >>= 1
        return R

    @classmethod
    def scalar(cls, label):
        return int.from_bytes(hashlib.sha256(b'TFN pool test key/' + label).digest(), 'big') % (cls.n - 1) + 1

    @classmethod
    def pub(cls, d):
        x, y = cls.mul(d, cls.G)
        return b'\x04' + x.to_bytes(32, 'big') + y.to_bytes(32, 'big')

    @classmethod
    def sign(cls, d, msg):
        n = cls.n
        e = int.from_bytes(hashlib.sha256(msg).digest(), 'big')
        x, h1 = d.to_bytes(32, 'big'), (e % n).to_bytes(32, 'big')
        V, K = b'\x01' * 32, b'\x00' * 32
        K = hmac.new(K, V + b'\x00' + x + h1, hashlib.sha256).digest()
        V = hmac.new(K, V, hashlib.sha256).digest()
        K = hmac.new(K, V + b'\x01' + x + h1, hashlib.sha256).digest()
        V = hmac.new(K, V, hashlib.sha256).digest()
        while True:
            V = hmac.new(K, V, hashlib.sha256).digest()
            k = int.from_bytes(V, 'big')
            if 1 <= k < n:
                r = cls.mul(k, cls.G)[0] % n
                s = pow(k, n - 2, n) * (e + r * d) % n
                if r and s:
                    return r.to_bytes(32, 'big') + s.to_bytes(32, 'big')
            K = hmac.new(K, V + b'\x00', hashlib.sha256).digest()
            V = hmac.new(K, V, hashlib.sha256).digest()


# ---------------------------------------------------------------- the pool vectors
def drbg(label, n):
    """Fixed TEST bytes for a label (SHA-256 in counter mode)."""
    out, i = b'', 0
    while len(out) < n:
        out += hashlib.sha256(b'TFN pool vectors 1/' + label.encode() + struct.pack('<I', i)).digest()
        i += 1
    return out[:n]


class TestKey:
    def __init__(self, label):
        self.d = P256.scalar(label.encode())
        self.pub = P256.pub(self.d)
        self.id = pc.device_id(self.pub)
        self.kid = pc.key_id(self.pub)

    def sign(self, msg):
        return P256.sign(self.d, msg)


SIGS = []  # (public key, message, r || s): openssl verifies each one


def signed_roster(r, key, extra=(), body_fn=None):
    body = pool.roster_body(r, extra)
    if body_fn:
        body = body_fn(body)
    rs = key.sign(body)
    SIGS.append((key.pub, body, rs))
    return body + pool.sig_record(key.pub, rs)


def mutate_sig(data, key):
    """data (a roster with a changed body) with a new signature over the bytes before its last record."""
    cut = data.rfind(b'\x7f\x48\x00')
    body = data[:cut]
    rs = key.sign(body)
    SIGS.append((key.pub, body, rs))
    return body + pool.sig_record(key.pub, rs)


def make_vectors():
    SIGS.clear()
    A, R, X = TestKey('admin A'), TestKey('admin R'), TestKey('not an admin')
    A2 = TestKey('admin A2')
    dev = [TestKey('board %d' % i) for i in range(1, 19)]
    macs = [bytes([0x02, 0, 0, 0, 0, i]) for i in range(1, 19)]
    pool_id, pool_k = drbg('pool id', 16), drbg('pool key', 32)
    other_id = drbg('other pool id', 16)
    names = ['BOARD ONE', 'BOARD TWO', '\u05dc\u05d5\u05d7 3']

    def mgr_member():
        return pool.Member(A.id, A.pub, pool.MANAGER_MAC, pool.ROLE_MANAGER, 0, 'MANAGER')

    def board(i, role=pool.ROLE_MEMBER):
        return pool.Member(dev[i].id, dev[i].pub, macs[i], role, 0, names[i] if i < 3 else 'BOARD %d' % (i + 1))

    def roster(version=7, pid=pool_id, admins=(A.pub, R.pub), members=None, lan='http://198.51.100.7:8080',
               vpn='http://203.0.113.1:8080', name='TEST POOL'):
        ms = members if members is not None else [mgr_member(), board(0), board(1, pool.ROLE_CHILD), board(2)]
        return pool.Roster(pid, name, version, 2, pool.F_LEGACY_BEACON, list(admins), ms, lan, vpn, 1700000000)

    v = {'format': 'pool_v1', 'about': 'The shared vectors of the pools (TFN OS P4 stage 2). Made by '
                                       'tests/unit/test_pool_crypto.py --update. All keys are TEST keys from fixed '
                                       'labels. Hex = lower case. All numbers little endian.'}
    v['notes'] = NOTES
    # ---- the published vectors
    v['x25519'] = {'rfc7748_mul': [{'scalar': k, 'u': u, 'out': o} for k, u, o in X25519_RFC],
                   'rfc7748_iter': [{'n': n, 'out': o} for n, o in X25519_ITER], 'rfc7748_dh': X25519_DH}
    v['chacha20'] = {'block': CHACHA_BLOCK, 'enc': CHACHA_ENC}
    v['poly1305'] = [POLY]
    v['aead'] = [AEAD]
    v['hkdf'] = HKDF_RFC
    v['siphash'] = {'k': bytes(range(16)).hex(), 'about': 'out[n] = SipHash-2-4(k, the bytes 00 01 .. n-1)',
                    'out': siphash_vectors()}
    # ---- the IDs and the keys made from the pool key
    v['ids'] = [{'name': n, 'pub': k.pub.hex(), 'device_id': k.id.hex(), 'kid': k.kid.hex()}
                for n, k in (('admin A', A), ('admin R', R), ('board 1', dev[0]))]
    v['derive'] = []
    for ep in (2, 1):
        ks = pool.derive(pool_k, pool_id, ep)
        v['derive'].append({'pool_k': pool_k.hex(), 'pool': pool_id.hex(), 'epoch': ep, 'link': ks.link.hex(),
                            'ptag': ks.ptag, 'beacon': ks.beacon.hex(), 'hint': ks.hint.hex()})
    keys, keys_prev = pool.derive(pool_k, pool_id, 2), pool.derive(pool_k, pool_id, 1)
    other_keys = pool.derive(drbg('other pool key', 32), other_id, 0)
    if other_keys.ptag == keys.ptag:
        raise SystemExit('the test pools have the same pool tag: change a label')
    # ---- the roster
    full = signed_roster(roster(), A)
    rp = pool.roster_parse(full)
    v['roster'] = {
        'file': full.hex(), 'signer': A.pub.hex(), 'kid': A.kid.hex(), 'signed_len': len(rp.signed),
        'fields': {'pool': pool_id.hex(), 'name': 'TEST POOL', 'v': 7, 'epoch': 2, 'flags': 1,
                   'admins': [A.pub.hex(), R.pub.hex()], 'lan': rp.lan, 'vpn': rp.vpn, 'time': 1700000000,
                   'members': [m.to_json() for m in rp.members]}}
    minimal = signed_roster(roster(admins=(A.pub,), members=[board(0)], lan=None, vpn=None, name='T'), A)
    v['roster_min'] = {'file': minimal.hex(), 'signer': A.pub.hex()}

    def flip(data, i, x=1):
        b = bytearray(data)
        b[i] ^= x
        return bytes(b)

    rec = full[8:]
    cases = [('ok', full, True), ('min', minimal, True),
             ('skip_0x90', signed_roster(roster(), A, extra=[(0x90, b'later field')]), True),
             ('magic', flip(full, 0), False), ('cut', full[:-1], False), ('cut_header', full[:9], False),
             ('type_0x10', signed_roster(roster(), A, extra=[(0x10, b'x')]), False),
             ('two_sigs', full + full[-75:], False),
             ('after_sig_0x90', full + pool.tlv(0x90, b'x'), False),
             ('no_sig', full[:-75], False),
             ('no_pool_id', mutate_sig(MAGIC_ONLY + rec[19:], A), False),
             ('two_pool_ids', mutate_sig(MAGIC_ONLY + rec[:19] + rec, A), False),
             ('name_empty', signed_roster(roster(), A, body_fn=lambda b: b.replace(
                 pool.tlv(2, b'TEST POOL'), pool.tlv(2, b''))), False),
             ('name_25', signed_roster(roster(), A, body_fn=lambda b: b.replace(
                 pool.tlv(2, b'TEST POOL'), pool.tlv(2, b'N' * 25))), False),
             ('name_24', signed_roster(roster(name='N' * 24), A), True),
             ('admins_3', signed_roster(roster(admins=(A.pub, R.pub, X.pub)), A), False),
             ('admin_twice', signed_roster(roster(admins=(A.pub, A.pub)), A), False),
             ('members_0', signed_roster(roster(members=[]), A), False),
             ('members_17', signed_roster(roster(members=[board(i) for i in range(17)]), A), True),
             ('members_18', signed_roster(roster(members=[board(i) for i in range(18)]), A), False),
             ('member_role_3', signed_roster(roster(members=[board(0, 3)]), A), False),
             ('member_mac_twice', signed_roster(roster(members=[board(0), pool.Member(
                 dev[1].id, dev[1].pub, macs[0], 0, 0, 'X')]), A), False),
             ('member_id_twice', signed_roster(roster(members=[board(0), pool.Member(
                 dev[0].id, dev[0].pub, macs[1], 0, 0, 'X')]), A), False),
             ('member_96', signed_roster(roster(), A, body_fn=lambda b: b.replace(
                 pool.tlv(7, board(0).record()), pool.tlv(7, board(0).record()[:96]))), False),
             ('version_3_bytes', signed_roster(roster(), A, body_fn=lambda b: b.replace(
                 pool.tlv(3, struct.pack('<I', 7)), pool.tlv(3, b'\x07\x00\x00'))), False),
             ('url_63', signed_roster(roster(lan='http://' + 'a' * 56), A), True),
             ('url_64', signed_roster(roster(), A, body_fn=lambda b: b.replace(
                 pool.tlv(8, pool.mgr_value('http://198.51.100.7:8080', 'http://203.0.113.1:8080')),
                 pool.tlv(8, b'h' * 64 + b'\0'))), False),
             ('url_no_zero', signed_roster(roster(), A, body_fn=lambda b: b.replace(
                 pool.tlv(8, pool.mgr_value('http://198.51.100.7:8080', 'http://203.0.113.1:8080')),
                 pool.tlv(8, b'http://x'))), False),
             ('record_past_end', full[:-75] + b'\x7f\x49\x00' + full[-72:], False),
             ('sig_71', full[:-75] + b'\x7f\x47\x00' + full[-72:-1], False),
             ('mgr_twice', signed_roster(roster(), A, extra=[(8, b'\0')]), False),
             ('size_4097', signed_roster(roster(), A, extra=[(0x90, b'\0' * 4000)]), False)]
    v['roster_parse'] = []
    for name, data, ok in cases:
        try:
            pool.roster_parse(data)
            got = True
        except pool.RosterError:
            got = False
        expect(got == ok, 'roster_parse %s: %s, not %s' % (name, got, ok))
        v['roster_parse'].append({'name': name, 'file': data.hex(), 'ok': ok})
    # ---- the rules of the roster (design 2.3)
    v8 = signed_roster(roster(8), A)
    bad8 = flip(v8, len(v8) - 5)
    rules = [
        ('take', full, v8, dev[0], 7, None, 'take'),
        ('same_version', full, signed_roster(roster(7, members=[mgr_member(), board(0)]), A), dev[0], 7, None, 'old'),
        ('older', full, signed_roster(roster(6), A), dev[0], 7, None, 'old'),
        ('removed', full, signed_roster(roster(8, members=[mgr_member(), board(1)]), A), dev[0], 7, None, 'removed'),
        ('other_pool', full, signed_roster(roster(8, pid=other_id), A), dev[0], 7, None, 'other'),
        ('not_admin', full, signed_roster(roster(8, admins=(X.pub, R.pub)), X), dev[0], 7, None, 'admin'),
        ('bad_sig', full, bad8, dev[0], 7, None, 'sig'),
        ('unknown_kid', full, v8[:-72] + X.kid + v8[-64:], dev[0], 7, None, 'sig'),
        ('r_replaces_a', full, signed_roster(roster(8, admins=(A2.pub, R.pub)), R), dev[0], 7, None, 'take'),
        ('form', full, v8[:-1], dev[0], 7, None, 'form'),
        ('no_pool', None, v8, dev[0], 0, None, 'other'),
        ('join', None, signed_roster(roster(1), A), dev[0], 9, A.pub, 'take'),
        ('join_other_admin', None, signed_roster(roster(1), R), dev[0], 0, A.pub, 'admin'),
        ('join_not_member', None, signed_roster(roster(1, members=[board(1)]), A), dev[0], 0, A.pub, 'member'),
    ]
    v['roster_rules'] = []
    for name, st, new, me, rv, ja, want in rules:
        got = pool.roster_accept(pool.roster_parse(st) if st else None, new, me.id, rv, ja)[0]
        expect(got == want, 'roster_accept %s: %s, not %s' % (name, got, want))
        v['roster_rules'].append({'name': name, 'stored': st.hex() if st else None, 'new': new.hex(),
                                  'own_id': me.id.hex(), 'rv': rv, 'join_admin': ja.hex() if ja else None,
                                  'want': want})
    # ---- link v4 frames
    v['frames'] = []
    for name, i, app, kind, ch, data, ctr, ks in (
            ('empty', 0, 1, 2, 0, b'', 0, keys), ('short', 0, 0x59, 3, 0x41, b'hello', 65536, keys),
            ('max', 1, 0x4e, 1, 6, drbg('frame max', 230), 0xfffffffe, keys),
            ('prev_epoch', 0, 1, 2, 0, b'old key', 7, keys_prev)):
        f = pool.frame_seal(ks, macs[i], app, kind, ch, data, ctr)
        v['frames'].append({'name': name, 'epoch': ks.epoch, 'mac': macs[i].hex(), 'app': app, 'kind': kind,
                            'ch': ch, 'data': data.hex(), 'ctr': ctr, 'frame': f.hex()})
    expect(len(h(v['frames'][2]['frame'])) == 250, 'a v4 frame with 230 data bytes has 250 bytes')
    good = pool.frame_seal(keys, macs[0], 0x59, 3, 0x41, b'hello', 65536)
    big = b'AL\x04\x01\x01' + bytes([231, 0, keys.ptag]) + bytes(4) + bytes(231)
    big += pc.siphash24(keys.link, macs[0] + big)
    v3 = b'AL\x03\x01\x01\x05\x00\x00\x01hello'
    members = [m.mac for m in rp.members]
    opens = [('ok', macs[0], good, 'ok'), ('other_pool', macs[0], pool.frame_seal(other_keys, macs[0], 0x59, 3, 0x41,
                                                                                    b'hello', 65536), 'other'),
             ('ptag_byte', macs[0], flip(good, 7), 'other'), ('version_3', macs[0], v3, 'form'),
             ('cut', macs[0], good[:-1], 'form'), ('long', macs[0], good + b'\0', 'form'),
             ('len_231', macs[0], big, 'form'), ('short_7', macs[0], good[:7], 'form'),
             ('not_member', macs[9], pool.frame_seal(keys, macs[9], 0x59, 3, 0x41, b'hello', 65536), 'who'),
             ('wrong_mac', macs[1], good, 'bad'),
             ('prev_key', macs[0], h(v['frames'][3]['frame']), 'other' if keys_prev.ptag != keys.ptag else 'bad')]
    for i in range(len(good)):
        if i != 7 and i not in (0, 1, 2, 5):
            opens.append(('bit_%d' % i, macs[0], flip(good, i), 'bad'))
    v['frame_open'] = {'epoch': 2, 'members': [m.hex() for m in members], 'cases': []}
    for name, mac, f, want in opens:
        got = pool.frame_open(keys, mac, f, members)[0]
        expect(got == want, 'frame_open %s: %s, not %s' % (name, got, want))
        v['frame_open']['cases'].append({'name': name, 'mac': mac.hex(), 'frame': f.hex(), 'want': want})
    expect(pool.frame_open(keys_prev, macs[0], h(v['frames'][3]['frame']), members)[0] == 'ok',
           'the frame of the previous key opens with that key')
    # ---- the replay window
    seqs = {'basic': [5, 5, 6, 4, 4, 70, 7, 6, 6, 1000, 937, 936, 1001, 999, 1000],
            'jump': [0, 0xFFFFFFFF, 0xFFFFFFC0, 0xFFFFFFC1, 3],
            'window_edge': [100, 37, 36, 101, 38, 37, 165, 102, 101]}
    want = {'basic': [1, 0, 1, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 1, 0], 'jump': [1, 1, 1, 1, 0],
            'window_edge': [1, 1, 0, 1, 1, 0, 1, 1, 0]}
    v['window'] = []
    for name, seq in seqs.items():
        w = pool.Window()
        got = [int(w.check(c)) for c in seq]
        expect(got == want[name], 'window %s: %s' % (name, got))
        v['window'].append({'name': name, 'ctr': seq, 'ok': want[name]})
    # ---- the UDP packets of version 2
    call = pool.call_v2(keys, macs[0], pool.MANAGER_MAC, pool.OP_WAKE, 12345)
    inv = pool.invite_v2(keys, macs[1], 'PONG', 70000)
    expect(len(call) == 29 and len(inv) == 42, 'call v2 = 29 bytes, invite v2 = 42 bytes')
    v['udp'] = {'epoch': 2, 'call': {'from': macs[0].hex(), 'to': pool.MANAGER_MAC.hex(), 'op': pool.OP_WAKE,
                                     'ctr': 12345, 'pkt': call.hex()},
                'invite': {'from': macs[1].hex(), 'app': 'PONG', 'ctr': 70000, 'pkt': inv.hex()}, 'open': []}
    for name, pkt, wnt in (('call', call, 'ok'), ('invite', inv, 'ok'), ('call_bit_9', flip(call, 9), 'bad'),
                           ('call_from', flip(call, 3), 'bad'), ('call_ctr', flip(call, 17), 'bad'),
                           ('call_tag', flip(call, 28), 'bad'), ('invite_app', flip(inv, 12), 'bad'),
                           ('call_ptag', flip(call, 16), 'other'), ('call_cut', call[:-1], 'form'),
                           ('call_v1', b'AC\x01' + call[3:16], 'form'),
                           ('call_other_pool', pool.call_v2(other_keys, macs[0], pool.MANAGER_MAC, 5, 1), 'other')):
        got = pool.udp_open(keys, pkt)[0]
        expect(got == wnt, 'udp_open %s: %s, not %s' % (name, got, wnt))
        v['udp']['open'].append({'name': name, 'pkt': pkt.hex(), 'want': wnt})
    # ---- the beacons
    plain = pool.beacon_plain(macs[0], 'BOARD ONE', 'PONG', 1, dev[0].id)
    bea = pool.beacon_seal(keys, plain, drbg('beacon nonce', 12))
    plain2 = pool.beacon_plain(pool.MANAGER_MAC, 'MANAGER', '', 0, A.id)
    bea2 = pool.beacon_seal(keys, plain2, drbg('beacon nonce 2', 12))
    expect(len(bea) == 78, 'the beacon has 78 bytes')
    v['beacon'] = {'epoch': 2, 'list': [
        {'mac': macs[0].hex(), 'name': 'BOARD ONE', 'app': 'PONG', 'flags': 1, 'id': dev[0].id.hex(),
         'nonce': drbg('beacon nonce', 12).hex(), 'plain': plain.hex(), 'pkt': bea.hex()},
        {'mac': pool.MANAGER_MAC.hex(), 'name': 'MANAGER', 'app': '', 'flags': 0, 'id': A.id.hex(),
         'nonce': drbg('beacon nonce 2', 12).hex(), 'plain': plain2.hex(), 'pkt': bea2.hex()}], 'open': []}
    for name, pkt, wnt in (('ok', bea, 'ok'), ('hint', flip(bea, 3), 'other'), ('version', flip(bea, 2, 3), 'form'),
                           ('cut', bea[:-1], 'form'), ('nonce', flip(bea, 7), 'bad'), ('text', flip(bea, 30), 'bad'),
                           ('tag', flip(bea, 77), 'bad'),
                           ('other_pool', pool.beacon_seal(other_keys, plain, drbg('beacon nonce 3', 12)), 'other')):
        got = pool.beacon_open(keys, pkt)[0]
        expect(got == wnt, 'beacon_open %s: %s, not %s' % (name, got, wnt))
        v['beacon']['open'].append({'name': name, 'pkt': pkt.hex(), 'want': wnt})
    aj = pool.join_pkt(macs[3], dev[3].id, 'NEW BOARD')
    v['join_pkt'] = {'mac': macs[3].hex(), 'id': dev[3].id.hex(), 'name': 'NEW BOARD', 'pkt': aj.hex()}
    # ---- the SAS
    v['sas'] = []
    for i in range(4):
        em, eb = drbg('sas em %d' % i, 32), drbg('sas eb %d' % i, 32)
        nm, nb = drbg('sas nm %d' % i, 16), drbg('sas nb %d' % i, 16)
        n = pool.sas(em, eb, nm, nb)
        v['sas'].append({'em': em.hex(), 'eb': eb.hex(), 'nm': nm.hex(), 'nb': nb.hex(), 'sas': n,
                         'text': pool.sas_text(n)})
    # ---- a full JOIN (design 2.7.1) of board 1 into the pool of the roster of version 1
    em_priv, eb_priv = drbg('join em', 32), drbg('join eb', 32)
    em_pub, eb_pub = pc.x25519_pub(em_priv), pc.x25519_pub(eb_priv)
    nm, nb = drbg('join nm', 16), drbg('join nb', 16)
    bx_priv = drbg('board 1 box', 32)
    bx_pub = pc.x25519_pub(bx_priv)
    c = pool.commit(em_pub, nm)
    mb, mm = pool.msg_b(c, eb_pub, nb), pool.msg_m(c, eb_pub, nb, em_pub, nm)
    sig_b, sig_m = dev[0].sign(mb), A.sign(mm)
    bmsg = pool.box_msg(dev[0].id, bx_pub)
    bxs = dev[0].sign(bmsg)
    SIGS.extend([(dev[0].pub, mb, sig_b), (A.pub, mm, sig_m), (dev[0].pub, bmsg, bxs)])
    jr = signed_roster(roster(1), A)
    shared = pc.x25519_shared(em_priv, eb_pub)
    expect(shared == pc.x25519_shared(eb_priv, em_pub), 'JOIN: both sides get the same X25519 value')
    sk = pool.seal_key(shared, c)
    wk1 = drbg('wk board 1', 32)
    env = pool.env_seal(sk, jr, 2, pool_k, wk1, drbg('join env nonce', 12))
    expect(pool.env_open(sk, jr, env) == (2, pool_k, wk1), 'JOIN: the envelope opens')
    expect(pool.env_open(sk, full, env) is None, 'JOIN: the envelope does not open with another roster')
    v['join'] = {'admin': A.pub.hex(), 'mid': A.id.hex(), 'board_pk': dev[0].pub.hex(), 'board_id': dev[0].id.hex(),
                 'em_priv': em_priv.hex(), 'em_pub': em_pub.hex(), 'nm': nm.hex(),
                 'eb_priv': eb_priv.hex(), 'eb_pub': eb_pub.hex(), 'nb': nb.hex(),
                 'bx_priv': bx_priv.hex(), 'bx_pub': bx_pub.hex(), 'box_msg': bmsg.hex(), 'bxs': bxs.hex(),
                 'c': c.hex(), 'msg_b': mb.hex(), 'sig_b': sig_b.hex(), 'msg_m': mm.hex(), 'sig_m': sig_m.hex(),
                 'sas': pool.sas(em_pub, eb_pub, nm, nb), 'sas_text': pool.sas_text(pool.sas(em_pub, eb_pub, nm, nb)),
                 'shared': shared.hex(), 'seal_k': sk.hex(), 'roster': jr.hex(), 'roster_sha256': pc.sha256(jr).hex(),
                 'epoch': 2, 'pool_k': pool_k.hex(), 'wk': wk1.hex(), 'env': env.hex()}
    env_bad = [('ok', env, True), ('nonce', flip(env, 0), False), ('text', flip(env, 20), False),
               ('tag', flip(env, 95), False), ('cut', env[:-1], False)]
    v['join']['env_open'] = [{'name': n, 'env': e.hex(), 'ok': ok} for n, e, ok in env_bad]
    for n, e, ok in env_bad:
        expect((pool.env_open(sk, jr, e) is not None) == ok, 'envelope %s' % n)
    # ---- op=key: a new key sealed to the box key of board 1 (design 2.7.3)
    em2_priv = drbg('rekey em', 32)
    em2_pub = pc.x25519_pub(em2_priv)
    shared2 = pc.x25519_shared(em2_priv, bx_pub)
    expect(shared2 == pc.x25519_shared(bx_priv, em2_pub), 'op=key: the same X25519 value')
    sk2 = pool.seal_key(shared2, dev[0].id)
    new_k = drbg('pool key epoch 3', 32)
    r3 = signed_roster(pool.Roster(pool_id, 'TEST POOL', 9, 3, 1, [A.pub, R.pub], [mgr_member(), board(0)],
                                   None, None, 1700000100), A)
    env2 = pool.env_seal(sk2, r3, 3, new_k, wk1, drbg('rekey env nonce', 12))
    v['rekey'] = {'board_id': dev[0].id.hex(), 'bx_priv': bx_priv.hex(), 'bx_pub': bx_pub.hex(),
                  'em_priv': em2_priv.hex(), 'em_pub': em2_pub.hex(), 'shared': shared2.hex(), 'seal_k': sk2.hex(),
                  'roster': r3.hex(), 'epoch': 3, 'pool_k': new_k.hex(), 'wk': wk1.hex(), 'env': env2.hex()}
    # ---- the request header (design 2.6)
    v['web'] = []
    bn = drbg('board nonce', 16)
    for i, (ctr, method, uri) in enumerate(((1730000000123, 'POST', '/api/pool?op=set'),
                                            (1730000000124, 'GET', '/api/boards'),
                                            (1730000000200, 'POST', '/api/apps?op=install&id=pong&place=card'))):
        text = pool.web_text(bn, ctr, method, uri)
        hdr = pool.web_header(wk1, bn, A.id, ctr, method, uri)
        v['web'].append({'wk': wk1.hex(), 'bn': bn.hex(), 'mid': A.id.hex(), 'ctr': ctr, 'method': method,
                         'uri': uri, 'text': text.hex(), 'mac': pc.hmac_sha256(wk1, text).hex(), 'header': hdr})
    w0 = v['web'][0]
    checks = [('ok', w0['header'], 0, True), ('same_counter', w0['header'], w0['ctr'], False),
              ('lower_counter', w0['header'], w0['ctr'] + 1, False),
              ('other_uri', v['web'][1]['header'], 0, False),
              ('mac_bit', w0['header'][:-1] + ('0' if w0['header'][-1] != '0' else '1'), 0, False),
              ('other_mid', 'ff' + w0['header'][2:], 0, False),
              ('counter_plus', w0['header'].replace('.%d.' % w0['ctr'], '.+%d.' % w0['ctr']), 0, False),
              ('two_parts', w0['header'].rsplit('.', 1)[0], 0, False)]
    v['web_check'] = {'wk': wk1.hex(), 'bn': bn.hex(), 'mid': A.id.hex(), 'method': 'POST', 'uri': '/api/pool?op=set',
                      'cases': []}
    for name, hdr, last, ok in checks:
        got = pool.web_check(wk1, bn, A.id, last, 'POST', '/api/pool?op=set', hdr)[0]
        expect(got == ok, 'web_check %s: %s' % (name, got))
        v['web_check']['cases'].append({'name': name, 'header': hdr, 'last': last, 'ok': ok})
    return v


MAGIC_ONLY = pool.MAGIC

NOTES = [
    'roster: "TFNPOOL1", then records type(u8) length(u16) value, the signature record 0x7F last. The writer order: '
    '01 02 03 04 05, 06 (A, then R), 07 (the members), 08 when there is one, 09, 7F.',
    'roster 0x02: the pool name, 1..24 bytes of UTF-8, no 0 byte at the end. 0x07: ID(8) pk(65) MAC(6) role(1) '
    'flags(1) name(16: UTF-8, 0 bytes after it). 0x08: the LAN URL, one 0 byte, the VPN URL (each 0..63 bytes). '
    '0x09: u64 (seconds).',
    'roster 0x7F: key ID(8) + r(32) + s(32) (big endian, as fw_sign.py). The key ID = the first 8 bytes of '
    'SHA-256(the 65-byte point), as the key ID of the signed OTA. ECDSA P-256 / SHA-256 over all bytes before the '
    'type byte of the 0x7F record (the magic too). A signature with a high s is good.',
    'roster parse: a fault = an unknown type below 0x80, a record that goes past the end, a fixed type with another '
    'length (01:16 03:4 04:4 05:4 06:65 07:97 09:8 7F:72), 01 02 03 04 05 08 09 two times, 01 02 03 04 05 09 '
    'missing, 1..2 admin keys (not the same key two times, first byte 04), 1..17 members (no MAC and no ID two '
    'times, role 0..2, the pk starts with 04), 08 with no 0 byte or a URL over 63 bytes, a record after 7F, no 7F, '
    'a file over 4096 bytes. A type from 0x80 up (before 7F) is skipped.',
    'roster rules (roster_rules.want): the order of the checks = form; sig (the key ID names an admin key of the NEW '
    'roster and the signature verifies); then for a JOIN (join_admin given): admin (the signer is not the JOIN admin '
    'key), the version floor is NOT checked (a new pool after a person tap or the token JOIN), member (the own ID is '
    'not in it), take; else: other (no stored pool or another pool ID), admin (the signer is not an admin key of the '
    'STORED roster), old (version <= rv), removed (the own ID is not in it), take.',
    'derive: HKDF-SHA256(ikm = pool key, salt = pool ID, info = text || epoch u32): "TFN link4" 16, "TFN ptag" 1, '
    '"TFN beacon1" 32, "TFN hint" 4 bytes.',
    'frame v4: AL 04 app kind len ch ptag ctr(u32) data, tag = SipHash-2-4(link, sender MAC(6) || the frame bytes '
    'before the tag), the 8 bytes of the u64 result little endian. frame_open order: not "AL" 04 or under 8 bytes = '
    'form; byte 7 not our ptag = other; len > 230 or the size not 20 + len = form; the MAC not a roster MAC = who; '
    'the tag = bad; then the counter window = old. A frame of the previous epoch has the ptag of that epoch: in its '
    'hour the receiver also takes that ptag and checks the tag with the previous link key (frames[prev_epoch]).',
    'window: per member, hi + 64 bits (bit i = hi - i seen). The first counter is good and sets hi. c > hi = good '
    '(shift). hi - c >= 64 = old; a bit that is set = old.',
    'udp v2: the call = AC 02 from(6) to(6) op, the invite = AI 02 from(6) app(20, 0 at the end); then ptag, '
    'ctr(u32), SipHash(link, from(6) || all bytes before the tag). The counter is the same counter as the link '
    'frames of the sender, the receiver uses the same window. udp_open: the same order as frame_open.',
    'beacon AP 01: hint(4) nonce(12) ChaCha20-Poly1305(beacon key, nonce, plain, aad = the first 7 bytes "AP" 01 '
    'hint) = 43 bytes + tag(16) = 78 bytes. plain = MAC(6) name(16) app(12) flags(1) ID(8), texts with 0 bytes '
    'after. beacon_open: not 78 bytes or not AP 01 = form; another hint = other; the tag = bad.',
    'join packet AJ 01: MAC(6) ID(8) name(16) = 33 bytes, plain.',
    'SAS = u32 LITTLE endian of the first 4 bytes of SHA-256("TFNSAS1" || E_m || E_b || N_m || N_b) mod 1000000; '
    'the text "%03d %03d". C = SHA-256("TFNJC1" || E_m || N_m). sig_b over "TFNJ1" || C || E_b || N_b (the device '
    'key), sig_m over "TFNJ1" || C || E_b || N_b || E_m || N_m (the admin key), bxs over "TFNBX1" || ID || bx.',
    'seal: K = HKDF-SHA256(X25519 value, salt = C (JOIN) or the board ID (op=key), "TFNSEAL1", 32). An X25519 value '
    'of 32 zero bytes is refused. The envelope = nonce(12) || ChaCha20-Poly1305(K, nonce, epoch(u32) || pool key(32) '
    '|| wk(32), aad = SHA-256(the roster file)) || tag(16) = 96 bytes.',
    'web: X-TFN-Auth = <manager ID, 16 hex>.<counter, decimal, no sign>.<HMAC-SHA256(wk, "TFNWEB1" || bn(16) || '
    'counter u64 || method || " " || uri), 64 hex>, lower case hex. uri = the path and the query as sent. The board '
    'takes a counter higher than the last good one of this start. The manager ID = the device ID formula on admin '
    'key A: SHA-256("TFNID1" || A)[0..8].',
    'api: GET /api/pool (no web password needed: public values) = {"state", "pool", "name", "v", "epoch", "role", '
    '"strict", "legacy", "members", "mgr", "bx", "bxs", "bn", "v4": {...}} + "id" (the device ID), "mac" (12 hex) '
    'and "join" ("none", "hello", "wait", "allowed"). POST /api/pool?op=<op> with a JSON body of hex fields: '
    'hello {"admin", "mid", "c"} (+ "auth" = the token for the token JOIN) -> {"eb", "nb", "pk", "id", "bx", "bxs", '
    '"sig"}; reveal {"em", "nm", "sig"} -> {"ok", "join"}; set {"roster"} + "env" in a JOIN (409 {"error":"wait"} '
    'until ALLOW); key {"em", "env"} + "roster" (the roster first, then the key; aad = the new roster); webpass '
    '{"pw"}. set, key and webpass need X-TFN-Auth (set in an allowed JOIN does not). The same roster bytes again = '
    'ok, no change. An error = 4xx {"error": "<code>"}: 401 auth, 409 wait / no_join, 400 form, 403 the roster rule '
    '(sig, admin, old, other, member).',
    'console: "pool join" opens the JOIN screen; the board prints a line with "SAS" and the 3 + 3 digits at the '
    'reveal (and at "pool"); "pool allow <6 digits>" = the tap on ALLOW; "webpass set <pw>".',
]


def compare(old, new, path='$'):
    """The first places where two vector trees differ."""
    out = []
    if type(old) is not type(new):
        return ['%s: another type' % path]
    if isinstance(new, dict):
        for k in sorted(set(old) | set(new)):
            if k not in old or k not in new:
                out.append('%s.%s: only in the %s' % (path, k, 'new' if k in new else 'file'))
            else:
                out += compare(old[k], new[k], '%s.%s' % (path, k))
    elif isinstance(new, list):
        if len(old) != len(new):
            out.append('%s: %d items in the file, %d new' % (path, len(old), len(new)))
        for i, (a, b) in enumerate(zip(old, new)):
            out += compare(a, b, '%s[%d]' % (path, i))
    elif old != new:
        out.append('%s: not the same' % path)
    return out[:20]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--update', action='store_true')
    a = ap.parse_args()
    x, y = P256.G
    expect((y * y - x ** 3 + 3 * x - P256.b) % P256.p == 0, 'P-256: G is on the curve')
    check_published()
    d = tempfile.mkdtemp(prefix='tfn_pool_vec_')
    try:
        ns = siphash_openssl(siphash_vectors(), d)
        nc = check_cryptography()
        v = make_vectors()
        nsig = 0
        try:
            fw_sign.openssl()
            for pub, msg, rs in SIGS:
                expect(pc.p256_verify(pub, msg, rs), 'openssl verifies the deterministic signature %d' % nsig)
                nsig += 1
        except fw_sign.SignError:
            print('(no openssl: the signatures are not checked)')
    finally:
        for f in os.listdir(d):
            os.unlink(os.path.join(d, f))
        os.rmdir(d)
    text = json.dumps(v, indent=1, ensure_ascii=True) + '\n'
    if a.update:
        os.makedirs(os.path.dirname(VEC_FILE), exist_ok=True)
        with open(VEC_FILE, 'wb') as f:
            f.write(text.encode('ascii'))
        print('wrote %s (%d bytes)' % (os.path.relpath(VEC_FILE, ROOT), len(text)))
    else:
        try:
            with open(VEC_FILE, 'rb') as f:
                old = json.loads(f.read().decode('ascii'))
            diff = compare(old, json.loads(text))
            for ln in diff:
                expect(False, 'vector file: %s' % ln)
            if diff:
                print('(the vector file differs: a format change needs --update, and the C side must agree)')
        except FileNotFoundError:
            expect(False, 'no vector file %s (run with --update)' % os.path.relpath(VEC_FILE, ROOT))
    print('POOL CRYPTO PY %s (RFC 7748/8439/5869 + SipHash; %d SipHash values checked with openssl, %d checks with '
          'cryptography, %d signatures checked with openssl, %d roster files, %d frames)' % (
              'OK' if not FAILS else '%d FAILED' % len(FAILS), ns, nc, nsig, len(v['roster_parse']),
              len(v['frame_open']['cases'])))
    return 1 if FAILS else 0


if __name__ == '__main__':
    sys.exit(main())
