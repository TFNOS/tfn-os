#!/usr/bin/env python3
"""The pools of TFN OS (P4 stage 2): the formats, the roster, the JOIN and the pool data of the manager.

A POOL = a pool ID (16 bytes), a name, a pool key (32 bytes) and a ROSTER: the signed list of the members (device ID,
public key, MAC, role, name) and the admin keys. The board code is main/tfn_pool_core.c; the two sides give the same
bytes (tests/unit/vectors/pool_v1.json, made by tests/unit/test_pool_crypto.py). The "notes" of that file tell each
choice of the format that the design document does not give.

Parts:
  the roster file "TFNPOOL1" + TLV records + the signature      roster_body, roster_sign, roster_parse, roster_accept
  the keys made from the pool key                                derive
  link v4 frames, the UDP call and invite of version 2           frame_seal, frame_open, Window, call_v2, invite_v2
  the beacons 'A' 'P' (encrypted) and 'A' 'J' (a JOIN screen)    beacon_seal, beacon_open, join_pkt
  the JOIN (commit, SAS, signatures) and the secret envelope     Join, commit, sas, env_seal, env_open
  the request header of the pool manager (X-TFN-Auth)            web_header, web_check
  the pool data of the manager (pool.json, secrets.json)         Pool
Only the standard library and the openssl program (pool_crypto.py, fw_sign.py).
"""
import json
import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pool_crypto as pc  # noqa: E402

# ---------------------------------------------------------------- the roster file (design 2.2.1)
MAGIC = b'TFNPOOL1'
T_ID, T_NAME, T_VER, T_EPOCH, T_FLAGS, T_ADMIN, T_MEMBER, T_MGR, T_TIME = 1, 2, 3, 4, 5, 6, 7, 8, 9
T_SIG = 0x7F
F_LEGACY_BEACON = 1
ROLE_MEMBER, ROLE_MANAGER, ROLE_CHILD = 0, 1, 2
ROLES = ('member', 'manager', 'child')
MAX_MEMBERS = 17
MAX_ADMINS = 2
NAME_MAX = 24
MEMBER_NAME = 16
URL_MAX = 63
MEMBER_LEN = 8 + 65 + 6 + 1 + 1 + MEMBER_NAME
SIG_LEN = 8 + 64
ROSTER_MAX = 4096
MANAGER_MAC = bytes.fromhex('fe4d47520001')
# the fixed length of each record type (None: the length is checked in roster_parse)
FIXED = {T_ID: 16, T_VER: 4, T_EPOCH: 4, T_FLAGS: 4, T_ADMIN: 65, T_MEMBER: MEMBER_LEN, T_TIME: 8, T_SIG: SIG_LEN}
# the results of roster_accept (design 2.3)
TAKE, REMOVED, FORM, SIG = 'take', 'removed', 'form', 'sig'
OTHER, ADMIN, OLD, MEMBER = 'other', 'admin', 'old', 'member'


class PoolError(Exception):
    pass


class RosterError(PoolError):
    def __init__(self, why):
        super().__init__(why)
        self.why = why


class Member:
    """A member of the roster. name: text (UTF-8, 16 bytes at most)."""

    def __init__(self, dev_id, pk, mac, role=ROLE_MEMBER, flags=0, name=''):
        self.id, self.pk, self.mac = bytes(dev_id), bytes(pk), bytes(mac)
        self.role, self.flags, self.name = role, flags, name

    def record(self):
        nb = self.name.encode('utf-8')
        if len(nb) > MEMBER_NAME:
            raise PoolError('the member name %r is longer than %d bytes' % (self.name, MEMBER_NAME))
        if len(self.id) != 8 or len(self.pk) != 65 or len(self.mac) != 6:
            raise PoolError('a member has an ID of 8 bytes, a key of 65 bytes and a MAC of 6 bytes')
        return self.id + self.pk + self.mac + bytes([self.role, self.flags]) + nb.ljust(MEMBER_NAME, b'\0')

    @classmethod
    def from_record(cls, v):
        name = v[81:97].split(b'\0')[0].decode('utf-8', 'replace')
        return cls(v[0:8], v[8:73], v[73:79], v[79], v[80], name)

    def to_json(self):
        return {'id': self.id.hex(), 'pk': self.pk.hex(), 'mac': self.mac.hex(), 'role': self.role,
                'flags': self.flags, 'name': self.name}


class Roster:
    """The fields of a roster file. signed = the bytes before the signature record; kid, rs = the signature."""

    def __init__(self, pool_id, name, version, epoch, flags, admins, members, lan=None, vpn=None, t=0):
        self.pool_id, self.name, self.version, self.epoch, self.flags = pool_id, name, version, epoch, flags
        self.admins, self.members, self.lan, self.vpn, self.time = list(admins), list(members), lan, vpn, t
        self.signed = self.kid = self.rs = self.raw = None

    def member_by_id(self, dev_id):
        for i, m in enumerate(self.members):
            if m.id == dev_id:
                return i
        return -1

    def member_by_mac(self, mac):
        for i, m in enumerate(self.members):
            if m.mac == mac:
                return i
        return -1


def tlv(t, v):
    if len(v) > 0xFFFF:
        raise PoolError('a record of more than 65535 bytes')
    return struct.pack('<BH', t, len(v)) + v


def mgr_value(lan, vpn):
    """The value of record 0x08: the LAN URL, one 0 byte, the VPN URL (each 0..63 bytes, no 0 byte inside)."""
    a, b = (lan or '').encode('ascii'), (vpn or '').encode('ascii')
    if len(a) > URL_MAX or len(b) > URL_MAX or b'\0' in a + b:
        raise PoolError('a manager URL has 63 bytes at most')
    return a + b'\0' + b


def roster_body(r, extra=()):
    """The roster bytes before the signature record, in the order of the writer: 01 02 03 04 05 06.. 07.. [08] 09.
    extra: (type, value) records to add before 09 (the tests: a type from 0x80 up that a parser skips)."""
    nb = r.name.encode('utf-8')
    if not 1 <= len(nb) <= NAME_MAX:
        raise PoolError('the pool name has 1 to %d bytes' % NAME_MAX)
    out = MAGIC + tlv(T_ID, r.pool_id) + tlv(T_NAME, nb) + tlv(T_VER, struct.pack('<I', r.version))
    out += tlv(T_EPOCH, struct.pack('<I', r.epoch)) + tlv(T_FLAGS, struct.pack('<I', r.flags))
    for a in r.admins:
        out += tlv(T_ADMIN, a)
    for m in r.members:
        out += tlv(T_MEMBER, m.record())
    if r.lan is not None or r.vpn is not None:
        out += tlv(T_MGR, mgr_value(r.lan, r.vpn))
    for t, v in extra:
        out += tlv(t, v)
    out += tlv(T_TIME, struct.pack('<Q', r.time))
    return out


def sig_record(admin_pub, rs):
    return tlv(T_SIG, pc.key_id(admin_pub) + rs)


def roster_sign(r, admin_pem, extra=()):
    """The signed roster file: the body + the signature record of the admin key (a PEM path; openssl)."""
    body = roster_body(r, extra)
    pub = pc.p256_pub(admin_pem)
    if pub not in r.admins:
        raise PoolError('the signing key is not an admin key of this roster')
    data = body + sig_record(pub, pc.p256_sign(admin_pem, body))
    if len(data) > ROSTER_MAX:
        raise PoolError('the roster has more than %d bytes' % ROSTER_MAX)
    return data


def roster_parse(data):
    """The fields of a roster file. Only the form: the signature is checked by roster_signer. RosterError(why) for
    each fault (an unknown type below 0x80, a record that goes past the end, a wrong count or length ...)."""
    if len(data) > ROSTER_MAX:
        raise RosterError('size')
    if data[:8] != MAGIC:
        raise RosterError('magic')
    seen = {}
    admins, members = [], []
    mgr = None
    sig = None
    pos = 8
    while pos < len(data):
        if sig is not None:
            raise RosterError('after_sig')
        if pos + 3 > len(data):
            raise RosterError('cut')
        t, n = struct.unpack_from('<BH', data, pos)
        v = data[pos + 3:pos + 3 + n]
        if len(v) != n:
            raise RosterError('cut')
        if t >= 0x80:
            pos += 3 + n
            continue
        if t not in FIXED and t not in (T_NAME, T_MGR):
            raise RosterError('type')
        if t in FIXED and n != FIXED[t]:
            raise RosterError('length')
        if t == T_ADMIN:
            if v[0] != 4:
                raise RosterError('admin')
            admins.append(v)
        elif t == T_MEMBER:
            if v[79] > ROLE_CHILD or v[8] != 4:
                raise RosterError('member')
            members.append(Member.from_record(v))
        elif t == T_SIG:
            sig = (v, pos)
        else:
            if t in seen:
                raise RosterError('twice')
            seen[t] = v
            if t == T_NAME and not 1 <= n <= NAME_MAX:
                raise RosterError('name')
            if t == T_MGR:
                parts = v.split(b'\0')
                if len(parts) != 2 or len(parts[0]) > URL_MAX or len(parts[1]) > URL_MAX:
                    raise RosterError('mgr')
                mgr = parts
        pos += 3 + n
    if sig is None:
        raise RosterError('no_sig')
    for t in (T_ID, T_NAME, T_VER, T_EPOCH, T_FLAGS, T_TIME):
        if t not in seen:
            raise RosterError('missing')
    if not 1 <= len(admins) <= MAX_ADMINS or len(set(admins)) != len(admins):
        raise RosterError('admins')
    if not 1 <= len(members) <= MAX_MEMBERS:
        raise RosterError('members')
    if len({m.mac for m in members}) != len(members) or len({m.id for m in members}) != len(members):
        raise RosterError('duplicate')
    r = Roster(seen[T_ID], seen[T_NAME].decode('utf-8', 'replace'), struct.unpack('<I', seen[T_VER])[0],
               struct.unpack('<I', seen[T_EPOCH])[0], struct.unpack('<I', seen[T_FLAGS])[0], admins, members,
               mgr[0].decode('ascii', 'replace') if mgr else None, mgr[1].decode('ascii', 'replace') if mgr else None,
               struct.unpack('<Q', seen[T_TIME])[0])
    r.signed = data[:sig[1]]
    r.kid, r.rs = sig[0][:8], sig[0][8:]
    r.raw = bytes(data)
    return r


def roster_signer(r, verify=pc.p256_verify):
    """The admin key of the roster r that made its signature (its key ID names it), or None."""
    for a in r.admins:
        if pc.key_id(a) == r.kid:
            return a if verify(a, r.signed, r.rs) else None
    return None


def roster_accept(stored, data, own_id, rv, join_admin=None, verify=pc.p256_verify):
    """The rules of design 2.3 on the board: (result, the parsed roster or None).
    stored: the Roster of the board or None; rv: the version floor; join_admin: the admin key of a JOIN (step 4).
    The order of the checks: form, signature (the key ID names an admin key of the NEW roster), then
      JOIN: the signer is the JOIN admin key (else ADMIN); the version floor is not used (a new pool, a person
            tapped ALLOW or the token JOIN); the own ID must be a member (else MEMBER)
      else: a stored pool with the same pool ID (else OTHER), the signer is an admin key of the STORED roster (else
            ADMIN), the version > rv (else OLD), the own ID a member (else REMOVED: the board leaves the pool)."""
    try:
        r = roster_parse(data)
    except RosterError:
        return FORM, None
    signer = roster_signer(r, verify)
    if signer is None:
        return SIG, r
    if join_admin is not None:
        if signer != join_admin:
            return ADMIN, r
        return (TAKE if r.member_by_id(own_id) >= 0 else MEMBER), r
    if stored is None or stored.pool_id != r.pool_id:
        return OTHER, r
    if signer not in stored.admins:
        return ADMIN, r
    if r.version <= rv:
        return OLD, r
    return (TAKE if r.member_by_id(own_id) >= 0 else REMOVED), r


# ---------------------------------------------------------------- the keys made from the pool key (design 2.2.4)
class Keys:
    def __init__(self, link, ptag, beacon, hint, epoch):
        self.link, self.ptag, self.beacon, self.hint, self.epoch = link, ptag, beacon, hint, epoch


def derive(pool_k, pool_id, epoch):
    """HKDF-SHA256(ikm = the pool key, salt = the pool ID, info = text || epoch (u32 little endian))."""
    e = struct.pack('<I', epoch)
    return Keys(pc.hkdf(pool_k, pool_id, b'TFN link4' + e, 16), pc.hkdf(pool_k, pool_id, b'TFN ptag' + e, 1)[0],
                pc.hkdf(pool_k, pool_id, b'TFN beacon1' + e, 32), pc.hkdf(pool_k, pool_id, b'TFN hint' + e, 4), epoch)


# ---------------------------------------------------------------- link v4 (design 2.4)
LINK_DATA_MAX = 230
V4_HDR = 12
V4_TAG = 8
DROP_OK, DROP_FORM, DROP_OTHER, DROP_WHO, DROP_BAD, DROP_OLD = 'ok', 'form', 'other', 'who', 'bad', 'old'


def frame_seal(keys, mac, app, kind, ch, data, ctr):
    """A link v4 frame: 'A' 'L' 4 app kind len ch ptag counter(u32) data, then SipHash-2-4(K_link, MAC || frame)."""
    if len(data) > LINK_DATA_MAX:
        raise PoolError('link data of more than %d bytes' % LINK_DATA_MAX)
    f = b'AL' + bytes([4, app, kind, len(data), ch, keys.ptag]) + struct.pack('<I', ctr) + bytes(data)
    return f + pc.siphash24(keys.link, bytes(mac) + f)


def frame_open(keys, mac, frame, members=None):
    """The check of a frame (design 2.4.2, without the counter window): (code, fields).
    The order: not 'A' 'L' 4 or shorter than 8 = form; the pool tag byte = other; the length = form; the sender MAC
    not in members (a list of MACs; None = no check) = who; the tag = bad."""
    if len(frame) < 8 or frame[:3] != b'AL\x04':
        return DROP_FORM, None
    if frame[7] != keys.ptag:
        return DROP_OTHER, None
    n = frame[5]
    if n > LINK_DATA_MAX or len(frame) != V4_HDR + n + V4_TAG:
        return DROP_FORM, None
    if members is not None and bytes(mac) not in members:
        return DROP_WHO, None
    if not pc.same(pc.siphash24(keys.link, bytes(mac) + frame[:-V4_TAG]), frame[-V4_TAG:]):
        return DROP_BAD, None
    return DROP_OK, {'app': frame[3], 'kind': frame[4], 'ch': frame[6], 'ctr': struct.unpack_from('<I', frame, 8)[0],
                     'data': frame[V4_HDR:V4_HDR + n]}


class Window:
    """The replay window of one sender (design 2.4.3): the highest counter and 64 bits (bit i = highest - i seen).
    The first counter sets the floor; a jump forward is good; a counter 64 or more below the highest, or a counter
    seen before, is old."""

    def __init__(self):
        self.hi = None
        self.bits = 0

    def check(self, c):
        if self.hi is None:
            self.hi, self.bits = c, 1
            return True
        if c > self.hi:
            d = c - self.hi
            self.bits = ((self.bits << d) | 1) & ((1 << 64) - 1) if d < 64 else 1
            self.hi = c
            return True
        d = self.hi - c
        if d >= 64 or (self.bits >> d) & 1:
            return False
        self.bits |= 1 << d
        return True


# ---------------------------------------------------------------- the UDP packets of version 2 (design 2.4.5)
C_LEN1, I_LEN1, I_NAME = 16, 29, 20
OP_WAKE = 5


def udp_tag(keys, body, ctr):
    """body (version 2) + pool tag + counter + SipHash over the sender MAC (body[3:9]) and the bytes before the tag."""
    b = bytes(body) + bytes([keys.ptag]) + struct.pack('<I', ctr)
    return b + pc.siphash24(keys.link, bytes(body[3:9]) + b)


def call_v2(keys, from_mac, to_mac, op, ctr):
    """'A' 'C' 2 from(6) to(6) op, then the pool tag, the counter and the tag: 29 bytes."""
    return udp_tag(keys, b'AC\x02' + bytes(from_mac) + bytes(to_mac) + bytes([op]), ctr)


def invite_v2(keys, from_mac, app, ctr):
    """'A' 'I' 2 from(6) app(20, 0 at the end), then the pool tag, the counter and the tag: 42 bytes."""
    a = app.encode('utf-8')[:I_NAME - 1].ljust(I_NAME, b'\0')
    return udp_tag(keys, b'AI\x02' + bytes(from_mac) + a, ctr)


def udp_open(keys, pkt):
    """(code, fields) of a call or an invite of version 2 (the same order as frame_open, no MAC list)."""
    if len(pkt) < 3 or pkt[0:1] != b'A' or pkt[1:2] not in (b'C', b'I') or pkt[2] != 2:
        return DROP_FORM, None
    body = C_LEN1 if pkt[1:2] == b'C' else I_LEN1
    if len(pkt) < body + 1:
        return DROP_FORM, None
    if pkt[body] != keys.ptag:
        return DROP_OTHER, None
    if len(pkt) != body + 13:
        return DROP_FORM, None
    if not pc.same(pc.siphash24(keys.link, pkt[3:9] + pkt[:-8]), pkt[-8:]):
        return DROP_BAD, None
    out = {'from': pkt[3:9], 'ctr': struct.unpack_from('<I', pkt, body + 1)[0]}
    if pkt[1:2] == b'C':
        out.update(to=pkt[9:15], op=pkt[15])
    else:
        out.update(app=pkt[9:29].split(b'\0')[0].decode('utf-8', 'replace'))
    return DROP_OK, out


# ---------------------------------------------------------------- the beacons (design 2.5)
AP_LEN = 3 + 4 + 12 + 43 + 16
AJ_LEN = 3 + 6 + 8 + 16


def beacon_plain(mac, name, app, flags, dev_id):
    """The 43 plain bytes of an 'A' 'P' beacon: MAC 6, name 16, app 12, flags 1, ID 8 (texts with 0 bytes after)."""
    nb, ab = name.encode('utf-8')[:16], app.encode('utf-8')[:12]
    return bytes(mac) + nb.ljust(16, b'\0') + ab.ljust(12, b'\0') + bytes([flags]) + bytes(dev_id)


def beacon_seal(keys, plain, nonce=None):
    """'A' 'P' 1 hint(4) nonce(12) ChaCha20-Poly1305(K_beacon, nonce, plain 43, aad = the first 7 bytes) tag(16)."""
    if len(plain) != 43:
        raise PoolError('the plain part of a beacon has 43 bytes')
    nonce = nonce or pc.rand(12)
    head = b'AP\x01' + keys.hint
    return head + nonce + pc.aead_seal(keys.beacon, nonce, plain, head)


def beacon_open(keys, pkt):
    """(code, the 43 plain bytes): form (not 'A' 'P' 1 or not 78 bytes), other (not our hint), bad (the tag)."""
    if len(pkt) != AP_LEN or pkt[:3] != b'AP\x01':
        return DROP_FORM, None
    if pkt[3:7] != keys.hint:
        return DROP_OTHER, None
    plain = pc.aead_open(keys.beacon, pkt[7:19], pkt[19:], pkt[:7])
    return (DROP_OK, plain) if plain is not None else (DROP_BAD, None)


def beacon_fields(plain):
    return {'mac': plain[0:6], 'name': plain[6:22].split(b'\0')[0].decode('utf-8', 'replace'),
            'app': plain[22:34].split(b'\0')[0].decode('utf-8', 'replace'), 'flags': plain[34], 'id': plain[35:43]}


def join_pkt(mac, dev_id, name):
    """'A' 'J' 1 MAC(6) ID(8) name(16): the plain packet of a board with its JOIN screen open."""
    return b'AJ\x01' + bytes(mac) + bytes(dev_id) + name.encode('utf-8')[:16].ljust(16, b'\0')


def join_pkt_parse(pkt):
    if len(pkt) != AJ_LEN or pkt[:3] != b'AJ\x01':
        return None
    return {'mac': pkt[3:9], 'id': pkt[9:17], 'name': pkt[17:33].split(b'\0')[0].decode('utf-8', 'replace')}


# ---------------------------------------------------------------- the JOIN (design 2.7.1) and the envelope (2.7.3)
ENV_LEN = 12 + 68 + 16


def commit(em_pub, nm):
    return pc.sha256(b'TFNJC1' + em_pub + nm)


def sas(em_pub, eb_pub, nm, nb):
    """The 6 digits: u32 little endian of the first 4 bytes of SHA-256("TFNSAS1" || E_m || E_b || N_m || N_b),
    mod 1000000."""
    return struct.unpack('<I', pc.sha256(b'TFNSAS1' + em_pub + eb_pub + nm + nb)[:4])[0] % 1000000


def sas_text(n):
    return '%03d %03d' % (n // 1000, n % 1000)


def msg_b(c, eb_pub, nb):
    """The text that the device key of the board signs in the answer to op=hello."""
    return b'TFNJ1' + c + eb_pub + nb


def msg_m(c, eb_pub, nb, em_pub, nm):
    """The text that the admin key signs in op=reveal."""
    return b'TFNJ1' + c + eb_pub + nb + em_pub + nm


def box_msg(dev_id, bx_pub):
    """The text that the device key signs for the box key (bxs)."""
    return b'TFNBX1' + dev_id + bx_pub


def seal_key(shared, salt):
    """The key of the envelope: HKDF(X25519 value, salt = C (JOIN) or the board ID (op=key), "TFNSEAL1"), 32 bytes."""
    return pc.hkdf(shared, salt, b'TFNSEAL1', 32)


def env_seal(k, roster_file, epoch, pool_k, wk, nonce=None):
    """The envelope: nonce(12) || ChaCha20-Poly1305(k, nonce, epoch(u32) || pool key(32) || wk(32),
    aad = SHA-256(the roster file)) || tag(16) = 96 bytes."""
    nonce = nonce or pc.rand(12)
    plain = struct.pack('<I', epoch) + pool_k + wk
    return nonce + pc.aead_seal(k, nonce, plain, pc.sha256(roster_file))


def env_open(k, roster_file, env):
    """(epoch, pool key, wk) of an envelope, or None (a wrong length, a wrong key, another roster)."""
    if len(env) != ENV_LEN:
        return None
    plain = pc.aead_open(k, env[:12], env[12:], pc.sha256(roster_file))
    if plain is None:
        return None
    return struct.unpack('<I', plain[:4])[0], plain[4:36], plain[36:68]


class Join:
    """The manager side of one JOIN (design 2.7.1). The order: hello_body() -> POST op=hello; take_hello(answer);
    reveal_body() -> POST op=reveal; the person compares the SAS; set_body() -> POST op=set."""

    def __init__(self, admin_pem, em_priv=None, nm=None):
        self.admin_pem = admin_pem
        self.admin = pc.p256_pub(admin_pem)
        self.mid = pc.device_id(self.admin)
        self.em_priv = em_priv or pc.rand(32)
        self.em_pub = pc.x25519_pub(self.em_priv)
        self.nm = nm or pc.rand(16)
        self.c = commit(self.em_pub, self.nm)
        self.board = None

    def hello_body(self, token=None):
        b = {'admin': self.admin.hex(), 'mid': self.mid.hex(), 'c': self.c.hex()}
        if token:
            b['auth'] = token
        return b

    def take_hello(self, ans, pin_pk=None):
        """Check the answer of the board: its ID = the ID of its key, sig_b and bxs verify with its device key, and
        the key is the pinned key (from /api/id) when one is given. Returns the fields of the board."""
        try:
            f = {k: bytes.fromhex(ans[k]) for k in ('eb', 'nb', 'pk', 'id', 'bx', 'bxs', 'sig')}
        except (KeyError, ValueError, TypeError):
            raise PoolError('the answer to op=hello has a missing or bad field')
        if len(f['eb']) != 32 or len(f['nb']) != 16 or len(f['bx']) != 32 or len(f['id']) != 8:
            raise PoolError('the answer to op=hello has a field of a wrong length')
        if pc.device_id(f['pk']) != f['id']:
            raise PoolError('the board ID is not the ID of its device key')
        if pin_pk is not None and f['pk'] != pin_pk:
            raise PoolError('the device key of the board is not the key that /api/id shows')
        if not pc.p256_verify(f['pk'], msg_b(self.c, f['eb'], f['nb']), f['sig']):
            raise PoolError('the JOIN signature of the board does not verify')
        if not pc.p256_verify(f['pk'], box_msg(f['id'], f['bx']), f['bxs']):
            raise PoolError('the box key signature of the board does not verify')
        self.board = f
        return f

    def reveal_body(self):
        b = self.board
        sig = pc.p256_sign(self.admin_pem, msg_m(self.c, b['eb'], b['nb'], self.em_pub, self.nm))
        return {'em': self.em_pub.hex(), 'nm': self.nm.hex(), 'sig': sig.hex()}

    def sas(self):
        return sas(self.em_pub, self.board['eb'], self.nm, self.board['nb'])

    def set_body(self, roster_file, epoch, pool_k, wk):
        k = seal_key(pc.x25519_shared(self.em_priv, self.board['eb']), self.c)
        return {'roster': roster_file.hex(), 'env': env_seal(k, roster_file, epoch, pool_k, wk).hex()}


def key_body(bx_pub, board_id, roster_file, epoch, pool_k, wk, with_roster=True):
    """The body of op=key: a new ephemeral key E_m for each send, the envelope sealed to the box key of the board."""
    em_priv, em_pub = pc.x25519_keypair()
    k = seal_key(pc.x25519_shared(em_priv, bx_pub), board_id)
    b = {'em': em_pub.hex(), 'env': env_seal(k, roster_file, epoch, pool_k, wk).hex()}
    if with_roster:
        b['roster'] = roster_file.hex()
    return b


# ---------------------------------------------------------------- the request header (design 2.6)
HDR = 'X-TFN-Auth'


def web_text(bn, ctr, method, uri):
    """"TFNWEB1" || bn(16) || counter (u64 little endian) || method || " " || uri (the path and the query)."""
    return b'TFNWEB1' + bn + struct.pack('<Q', ctr) + method.encode('ascii') + b' ' + uri.encode('ascii')


def web_header(wk, bn, mid, ctr, method, uri):
    """The value of X-TFN-Auth: <manager ID hex>.<counter decimal>.<HMAC-SHA256 hex> (lower case hex)."""
    return '%s.%d.%s' % (mid.hex(), ctr, pc.hmac_sha256(wk, web_text(bn, ctr, method, uri)).hex())


def web_check(wk, bn, mid, last, method, uri, hdr):
    """The check of the board: (True, counter) when the header is good and the counter > last, else (False, None)."""
    try:
        m, c, mac = hdr.split('.')
        ctr = int(c)
        if c != str(ctr) or m != mid.hex() or not 0 <= ctr < 1 << 64:
            return False, None
        mac = bytes.fromhex(mac)
    except (ValueError, AttributeError):
        return False, None
    if ctr <= last or not pc.same(pc.hmac_sha256(wk, web_text(bn, ctr, method, uri)), mac):
        return False, None
    return True, ctr


class Counter:
    """The request counter of the manager: the time in ms, always higher than the last one."""

    def __init__(self):
        self.last = 0

    def next(self):
        self.last = max(self.last + 1, int(time.time() * 1000))
        return self.last


# ---------------------------------------------------------------- the pool data of the manager (design 2.10)
class Pool:
    """The pool of an admin. pool.json = the public values, secrets.json (mode 0600) = the pool key, the previous
    keys and the wk of each member, roster.bin = the last signed roster. A member is a dict: id, pk, mac, role,
    flags, name, bx (the box key) and ip (the last address)."""

    def __init__(self):
        self.pool_id = b''
        self.name = ''
        self.version = 0
        self.epoch = 0
        self.flags = 0
        self.admins = []
        self.members = []
        self.lan = self.vpn = None
        self.pool_k = b''
        self.prev = []
        self.wks = {}
        self.roster_file = b''

    @classmethod
    def create(cls, name, admin_pub, recovery_pub=None, flags=0):
        p = cls()
        p.pool_id, p.name, p.flags = pc.rand(16), name, flags
        if not 1 <= len(name.encode('utf-8')) <= NAME_MAX:
            raise PoolError('the pool name has 1 to %d bytes' % NAME_MAX)
        p.admins = [admin_pub] + ([recovery_pub] if recovery_pub else [])
        p.pool_k = pc.rand(32)
        return p

    def add_manager(self, name='MANAGER'):
        """The manager as a member: its ID and key = admin key A, the MAC of the radio hint, the role MANAGER."""
        a = self.admins[0]
        self.upsert({'id': pc.device_id(a).hex(), 'pk': a.hex(), 'mac': MANAGER_MAC.hex(), 'role': ROLE_MANAGER,
                     'flags': 0, 'name': name})

    def find(self, what):
        """A member by its ID, MAC, name or address (case does not matter)."""
        w = what.lower().replace(':', '')
        for m in self.members:
            if w in (m['id'], m['mac'], m['name'].lower(), (m.get('ip') or '').lower()):
                return m
        return None

    def upsert(self, m):
        for i, o in enumerate(self.members):
            if o['id'] == m['id']:
                o.update(m)
                return o
        if len(self.members) >= MAX_MEMBERS:
            raise PoolError('the pool has %d members (the limit)' % MAX_MEMBERS)
        self.members.append(m)
        return m

    def remove(self, dev_id_hex):
        self.members = [m for m in self.members if m['id'] != dev_id_hex]
        self.wks.pop(dev_id_hex, None)

    def new_key(self):
        """The next epoch with a new pool key (a removal). The previous key stays in secrets.json."""
        self.prev = ([{'epoch': self.epoch, 'k': self.pool_k.hex()}] + self.prev)[:4]
        self.epoch += 1
        self.pool_k = pc.rand(32)

    def wk(self, dev_id_hex):
        if dev_id_hex not in self.wks:
            self.wks[dev_id_hex] = pc.rand(32).hex()
        return bytes.fromhex(self.wks[dev_id_hex])

    def roster(self):
        ms = [Member(bytes.fromhex(m['id']), bytes.fromhex(m['pk']), bytes.fromhex(m['mac']), m['role'],
                     m.get('flags', 0), m['name']) for m in self.members]
        return Roster(self.pool_id, self.name, self.version, self.epoch, self.flags, self.admins, ms, self.lan,
                      self.vpn, int(time.time()))

    def sign(self, admin_pem):
        """A new roster version, signed. Returns the file bytes (also kept in roster_file)."""
        if not self.members:
            raise PoolError('a roster needs one member at least')
        self.version += 1
        self.roster_file = roster_sign(self.roster(), admin_pem)
        return self.roster_file

    def keys(self):
        return derive(self.pool_k, self.pool_id, self.epoch)

    def mid(self):
        return pc.device_id(self.admins[0])

    def to_json(self):
        return {'format': 1, 'pool': self.pool_id.hex(), 'name': self.name, 'v': self.version, 'epoch': self.epoch,
                'flags': self.flags, 'admins': [a.hex() for a in self.admins], 'lan': self.lan, 'vpn': self.vpn,
                'members': self.members}

    def save(self, d):
        os.makedirs(d, exist_ok=True)
        write_file(os.path.join(d, 'pool.json'), (json.dumps(self.to_json(), indent=1) + '\n').encode())
        sec = {'format': 1, 'pool_k': self.pool_k.hex(), 'prev': self.prev, 'wk': self.wks}
        write_file(os.path.join(d, 'secrets.json'), (json.dumps(sec, indent=1) + '\n').encode(), 0o600)
        if self.roster_file:
            write_file(os.path.join(d, 'roster.bin'), self.roster_file)

    @classmethod
    def load(cls, d):
        with open(os.path.join(d, 'pool.json'), 'rb') as h:
            j = json.loads(h.read().decode('utf-8'))
        with open(os.path.join(d, 'secrets.json'), 'rb') as h:
            s = json.loads(h.read().decode('utf-8'))
        p = cls()
        p.pool_id, p.name, p.version, p.epoch = bytes.fromhex(j['pool']), j['name'], j['v'], j['epoch']
        p.flags, p.admins, p.lan, p.vpn = j['flags'], [bytes.fromhex(a) for a in j['admins']], j['lan'], j['vpn']
        p.members = j['members']
        p.pool_k, p.prev, p.wks = bytes.fromhex(s['pool_k']), s['prev'], s['wk']
        rp = os.path.join(d, 'roster.bin')
        if os.path.exists(rp):
            with open(rp, 'rb') as h:
                p.roster_file = h.read()
        return p


REPLACE_TRIES = 20   # the replace of write_file tries again for 1 s (20 x 50 ms)


def write_file(path, data, mode=None):
    """Write a file through a temporary file of this process (the old file stays when the write fails). mode: the
    access bits of a new secret file (0600). Windows refuses a replace while another process reads the file (a virus
    scanner, an index, a reload of the pool by another process): the replace tries again for 1 s. A fault (also
    Ctrl+C) removes the temporary file: no copy of a secret stays next to the file."""
    tmp = '%s.%d.tmp' % (path, os.getpid())
    try:
        fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC | getattr(os, 'O_BINARY', 0), mode or 0o644)
        with os.fdopen(fd, 'wb') as h:
            h.write(data)
        if mode is not None:
            os.chmod(tmp, mode)
        replace_retry(tmp, path)
    except BaseException:
        try:
            os.remove(tmp)
        except OSError:
            pass
        raise


def replace_retry(src, dst):
    """os.replace(src, dst), again for 1 s while Windows refuses it (PermissionError)."""
    for i in range(REPLACE_TRIES):
        try:
            os.replace(src, dst)
            return
        except PermissionError:
            if i == REPLACE_TRIES - 1:
                raise
            time.sleep(0.05)
