"""The keys of the pool, the backup and the recovery (design 2.12): key A, key R, the encrypted files .tfnkey and
.tfnbak.

KEY A (the admin key) = keys/admin_a.pem (0600) in the data folder: the program signs with it with no person.
KEY R (the recovery key) is never on the disk in clear: openssl writes the PEM to its output, and the program keeps
only the encrypted file <pool name>-recovery.tfnkey and the public value (an admin key of the roster).

THE FORM OF AN ENCRYPTED FILE (.tfnkey = key R, .tfnbak = all of the pool):
  the magic (7: "TFNKEY1" or "TFNBAK1") | the KDF id (1) | the salt (16) | the KDF values (4) | the nonce (12)
  | ChaCha20-Poly1305 of the plain part (the key of the KDF; AAD = the 40 header bytes) | the tag (16)
  KDF 1 = scrypt: the values = log2 N, r, p, 0 (default N 2^15, r 8, p 1)
  KDF 2 = PBKDF2-HMAC-SHA256: the values = the rounds (u32 little endian; default 600 000), when the Python has no
          scrypt
  The plain part of .tfnkey = the PEM of key R. The plain part of .tfnbak = a ZIP (stored, no compression) of
  config.json, keys/*.pem and pool/* (a backup of an older poolmgr can have recover.json at the top too: restore
  puts it into pool/).

A SIGNATURE WITH A KEY IN MEMORY (key R, and the checks of a backup): openssl writes the key as an ENCRYPTED PKCS#8
file in a temporary folder (a random pass phrase in the environment of the openssl process), signs with it, and the
folder goes away. So no clear copy of the key is on the disk.

CHECK-BACKUP compares each file of a backup with the data folder (the files that a backup has): a file that is not
the same gives one line "BACKUP IS OLD: <the part>" (config.json: each value, but not the address of the local store,
which serve finds again).

RESTORE writes into a new data folder, or into a folder of poolmgr with no pool (only empty parts and the change
lock: pm_data.leftovers). It writes the files into a temporary folder in the data folder (keys/ and pool/ get their
own rights first: pm_data.private_tmp) and moves them into place (pool/ last).

BACKUP writes a new file, or replaces an older backup (a file that starts with TFNBAK1). It refuses each other file
and each name that ends with .tfnkey: a wrong name must not replace the encrypted file of key R. An older backup goes
only when the pass phrase of the new backup opens it, it is a backup of the same pool, and it is not newer than the
data folder (a higher roster version or key epoch, or another key A: for example the newest backup after a recover,
and an older data folder on a second computer); else the option --replace: two pools with the same name have the same
default file name. The .tfnkey and .tfnbak files get only the rights of this user, SYSTEM and the administrators
(pm_data.protect_file(); a WARN line when the rights do not set).

RECOVER (key A is lost, or a copy of it is out): key R signs a roster with a new key A2 in place of A and a new
manager member (the ID of A2); a new pool key (epoch + 1) and a new wk for each member. On each board: op=set with
the header of the OLD manager ID and the old wk (the board takes the roster: R is an admin key of its stored roster),
then op=key with the header of A2 (the new pool key and wk, sealed to the box key of the board). The file
pool/recover.json (0600; pool/ has its own rights) keeps the old wk values of the boards that did not answer:
"recover" again sends to them.

The pass phrase comes from poolmgr.py (a question, or the variable TFN_POOLMGR_PHRASE for the tests). No function
writes a key value, a pass phrase, a pool key or a wk into a log or into its output: only the key IDs.
"""
import hashlib
import io
import json
import os
import re
import secrets
import struct
import subprocess
import sys
import tempfile
import time
import zipfile

import pm_board
import pm_data
from pm_data import Data, write_file
from pm_shared import Failed, InputError, fw_sign, pool, pool_crypto as pc, pool_tool

DONE = True

KEY = b'TFNKEY1'
BAK = b'TFNBAK1'
KINDS = (KEY, BAK)
KDF_SCRYPT, KDF_PBKDF2 = 1, 2
SCRYPT = (15, 8, 1)          # log2 N, r, p
PBKDF2_ROUNDS = 600000
SALT, NONCE, TAG = 16, 12, 16
HEAD = 7 + 1 + SALT + 4 + NONCE
MIN_PHRASE = 12
MAX_BAK = 16 * 1024 * 1024   # the limit of the plain part of a backup
KDF_PREFER = None            # None = scrypt when the Python has it; 'pbkdf2' = always PBKDF2 (the tests)
RECOVER_FILE = 'recover.json'
PASS_ENV = 'TFN_POOLMGR_KP'  # the name of the variable that gives openssl the pass phrase of a temporary key file
# the names in a backup (no folder above, no other place)
BAK_NAME = re.compile(r'^(config\.json|recover\.json|keys/[A-Za-z0-9_][A-Za-z0-9_.-]*\.pem|'
                      r'pool/[A-Za-z0-9_][A-Za-z0-9_.-]*)$')
# the words of each file of a backup for a person (check-backup)
BAK_WHAT = {'recover.json': 'the state of the last recover (recover.json of an older poolmgr)',
            'pool/recover.json': 'the state of the last recover (pool/recover.json)',
            'keys/admin_a.pem': 'key A (keys/admin_a.pem)',
            'keys/store_pool.pem': 'the pool store key (keys/store_pool.pem)',
            'pool/pool.json': 'the members and the roster (pool/pool.json)',
            'pool/secrets.json': 'the pool key and the request keys (pool/secrets.json)',
            'pool/roster.bin': 'the signed roster (pool/roster.bin)',
            'pool/webpass.json': 'the web passwords of the boards (pool/webpass.json)',
            'pool/store_tok.json': 'the store tokens of the boards (pool/store_tok.json)',
            'pool/friends_out.json': 'the friend entries that wait for their removal (pool/friends_out.json)'}
CFG_WHAT = {'store': 'the store mode', 'port': 'the TCP port of serve', 'auto_update': 'auto_update',
            'channel': 'the channel (the web site, the repo and the official keys)'}
CFG_SKIP = ('lan_url', 'format')   # (serve finds the address of the local store again)
PARTS = ('config.json', 'recover.json', 'keys', 'pool')   # the parts of a backup in a data folder (pool last)


def say(text):
    """One line at once (sys.stdout is None in the service of Windows)."""
    print(text)
    if sys.stdout is not None:
        sys.stdout.flush()


# ---------------------------------------------------------------- openssl with a key in memory
def ssl(args, data=None, env=None):
    """Run openssl with the bytes data as its input: its output. fw_sign.SignError for a fault."""
    e = dict(os.environ)
    e.update(env or {})
    try:
        r = subprocess.run([fw_sign.openssl()] + args, input=data, capture_output=True, timeout=60, env=e,
                           creationflags=pm_data.NO_WINDOW)
    except (OSError, subprocess.SubprocessError) as x:
        raise fw_sign.SignError('openssl %s: %s' % (args[0], x))
    if r.returncode:
        raise fw_sign.SignError('openssl %s: %s' % (args[0], r.stderr.decode('utf-8', 'replace').strip()[-300:]))
    return r.stdout


def new_pem():
    """A new P-256 private key as PEM bytes (the output of openssl: no file), with the named curve prime256v1
    (fw_sign.new_key)."""
    return fw_sign.new_key()


def pub_of(pem):
    """The 65-byte public point of a private key in memory (PEM bytes)."""
    return fw_sign.point_from_spki(ssl(['pkey', '-pubout', '-outform', 'DER'], pem))


def enc_pem(pem, kp):
    """The key as an ENCRYPTED PKCS#8 PEM (AES-256-CBC) with the pass phrase kp."""
    return ssl(['pkcs8', '-topk8', '-v2', 'aes-256-cbc', '-passout', 'env:' + PASS_ENV], pem, {PASS_ENV: kp})


def sign_mem(pem, msg):
    """r || s of an ECDSA P-256 / SHA-256 signature of msg with a key in memory (only an encrypted copy of the key
    goes into a temporary folder)."""
    kp = secrets.token_hex(32)
    enc = enc_pem(pem, kp)
    with tempfile.TemporaryDirectory(prefix='tfnpm_') as d:
        path = os.path.join(d, 'k.pem')
        fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, 'O_BINARY', 0), 0o600)
        with os.fdopen(fd, 'wb') as h:
            h.write(enc)
        der = ssl(['dgst', '-sha256', '-sign', path, '-passin', 'env:' + PASS_ENV], msg, {PASS_ENV: kp})
    return fw_sign.der_to_rs(der)


def key_check(pem, pub):
    """True: the key in memory signs a test text, and the public value pub verifies it (and not a changed text)."""
    text = b'TFN key check ' + os.urandom(16).hex().encode()
    rs = sign_mem(pem, text)
    return fw_sign.verify_raw(pub, text, rs) and not fw_sign.verify_raw(pub, text + b'.', rs)


def kid(pub):
    return pc.key_id(pub).hex()


# ---------------------------------------------------------------- the encrypted files
def has_scrypt():
    if KDF_PREFER == 'pbkdf2' or not hasattr(hashlib, 'scrypt'):
        return False
    try:
        hashlib.scrypt(b'x', salt=b's' * 16, n=16, r=1, p=1, dklen=32)
        return True
    except (ValueError, OSError):
        return False


def kdf_key(kdf, vals, salt, phrase):
    """The 32-byte key of the pass phrase. Failed for a KDF id or values that this program does not take."""
    pw = phrase.encode('utf-8')
    if kdf == KDF_SCRYPT:
        ln, r, p, z = vals
        if not (10 <= ln <= 20 and 1 <= r <= 16 and 1 <= p <= 4 and z == 0) or 128 * r * (1 << ln) > (64 << 20):
            raise Failed('the file has scrypt values that this program does not take')
        if not hasattr(hashlib, 'scrypt'):
            raise Failed('the file needs scrypt, and this Python has no hashlib.scrypt (for example the system Python '
                         'of macOS): install Python from python.org or Homebrew, and use it for this command')
        return hashlib.scrypt(pw, salt=salt, n=1 << ln, r=r, p=p, maxmem=256 * r * (1 << ln) + (1 << 20), dklen=32)
    if kdf == KDF_PBKDF2:
        rounds = struct.unpack('<I', vals)[0]
        if not 100000 <= rounds <= 10000000:
            raise Failed('the file has a PBKDF2 round count that this program does not take')
        return hashlib.pbkdf2_hmac('sha256', pw, salt, rounds, 32)
    raise Failed('the file has a KDF that this program does not know (%d)' % kdf)


def seal(kind, plain, phrase):
    """kind b'TFNKEY1' or b'TFNBAK1': the encrypted bytes (the KDF, the salt, the nonce, ChaCha20-Poly1305)."""
    if kind not in KINDS:
        raise InputError('an encrypted file is TFNKEY1 or TFNBAK1')
    if len(phrase) < MIN_PHRASE:
        raise InputError('the pass phrase must have %d characters or more' % MIN_PHRASE)
    salt, nonce = pc.rand(SALT), pc.rand(NONCE)
    if has_scrypt():
        kdf, vals = KDF_SCRYPT, bytes([SCRYPT[0], SCRYPT[1], SCRYPT[2], 0])
    else:
        kdf, vals = KDF_PBKDF2, struct.pack('<I', PBKDF2_ROUNDS)
    head = kind + bytes([kdf]) + salt + vals + nonce
    return head + pc.aead_seal(kdf_key(kdf, vals, salt, phrase), nonce, plain, head)


def open_(blob, phrase):
    """(kind, plain) of an encrypted file; Failed for a wrong pass phrase or a changed byte."""
    kind = bytes(blob[:7])
    if kind not in KINDS or len(blob) < HEAD + TAG:
        raise Failed('not a .tfnkey or .tfnbak file of poolmgr')
    head = bytes(blob[:HEAD])
    salt, vals, nonce = head[8:8 + SALT], head[8 + SALT:12 + SALT], head[HEAD - NONCE:]
    plain = pc.aead_open(kdf_key(head[7], vals, salt, phrase), nonce, bytes(blob[HEAD:]), head)
    if plain is None:
        raise Failed('the pass phrase is wrong, or the file is changed or damaged')
    return kind, plain


def write_new(path, blob):
    """Write a key file that must not exist (a key file is never overwritten), mode 0600; the file gets the rights of
    pm_data.protect_file() before its content (the current folder can give rights to other accounts). The result:
    the fault of the rights ('' = done). A fault in the write (also Ctrl+C) removes the part that this call wrote: no
    partial file stays."""
    try:
        fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, 'O_BINARY', 0), 0o600)
    except FileExistsError:
        raise InputError('%s exists: a key file is never overwritten' % path)
    try:
        with os.fdopen(fd, 'wb') as h:
            err = pm_data.protect_file(path)
            h.write(blob)
        return err
    except BaseException:
        try:
            os.remove(path)
        except OSError:
            pass
        raise


# ---------------------------------------------------------------- key A and key R
def make_admin(data):
    """Key A = keys/admin_a.pem (0600): its public value."""
    if os.path.exists(data.key_a):
        raise InputError('%s exists: a key is never overwritten' % data.key_a)
    data.make()
    pub = fw_sign.keygen(data.key_a)
    data.log('key A made: key ID %s' % kid(pub))
    return pub


def make_recovery(data, out, phrase):
    """Key R: only the encrypted file out (.tfnkey), never a clear copy on the disk. Gives the public value."""
    if os.path.exists(out):
        raise InputError('%s exists: a key file is never overwritten' % out)
    pem = new_pem()
    pub = pub_of(pem)
    if not key_check(pem, pub):
        raise Failed('the new key R does not sign: check openssl (poolmgr doctor)')
    blob = seal(KEY, pem, phrase)
    if open_(blob, phrase)[1] != pem:
        raise Failed('the encrypted key R does not open again')
    write_new(out, blob)
    data.log('key R made: key ID %s, only the encrypted file %s' % (kid(pub), os.path.basename(out)))
    return pub


# ---------------------------------------------------------------- the backup
def files_of(data):
    """{name in the backup: bytes} of the data folder: config.json, keys/*.pem, pool/* (and recover.json at the top
    of the data folder of an older poolmgr)."""
    out = {}
    for name in ('config.json', RECOVER_FILE):
        p = os.path.join(data.root, name)
        if os.path.isfile(p):
            out[name] = read(p)
    for sub, want in (('keys', lambda n: n.endswith('.pem')), ('pool', lambda n: not n.endswith('.tmp'))):
        d = os.path.join(data.root, sub)
        for n in sorted(os.listdir(d)) if os.path.isdir(d) else ():
            if os.path.isfile(os.path.join(d, n)) and want(n) and BAK_NAME.match('%s/%s' % (sub, n)):
                out['%s/%s' % (sub, n)] = read(os.path.join(d, n))
    return out


def read(path):
    with open(path, 'rb') as h:
        return h.read()


def zip_of(files):
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, 'w', zipfile.ZIP_STORED) as z:
        for name in sorted(files):
            zi = zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0))
            zi.compress_type = zipfile.ZIP_STORED
            z.writestr(zi, files[name])
    return buf.getvalue()


def unzip(plain):
    """{name: bytes} of the plain part of a backup. Failed for a damaged ZIP, a name that is not a name of a backup,
    two files with one name, or too many bytes."""
    try:
        z = zipfile.ZipFile(io.BytesIO(plain))
        infos = z.infolist()
    except (zipfile.BadZipFile, ValueError) as e:
        raise Failed('the backup has no good ZIP part (%s)' % e)
    out, total = {}, 0
    for zi in infos:
        if not BAK_NAME.match(zi.filename) or zi.filename in out:
            raise Failed('the backup has a file that is not part of a pool: %r' % zi.filename[:60])
        total += zi.file_size
        if total > MAX_BAK:
            raise Failed('the backup has more than %d bytes' % MAX_BAK)
        try:
            out[zi.filename] = z.read(zi)
        except (zipfile.BadZipFile, ValueError, NotImplementedError) as e:
            raise Failed('the backup file %s does not read (%s)' % (zi.filename, e))
    return out


def backup_target(out):
    """The full path of the output file of a backup: a new file, or an older backup (the file starts with TFNBAK1).
    InputError for a name that ends with .tfnkey, a folder, or a file that is not a backup (for example the key file
    of key R after a wrong completion of the name)."""
    path = os.path.abspath(out)
    if path.lower().endswith('.tfnkey'):
        raise InputError('%s: a backup does not go into a .tfnkey file (the key file of key R is never replaced): give '
                         'a name that ends with .tfnbak' % out)
    if os.path.isdir(path):
        raise InputError('%s is a folder: give the name of the backup file' % out)
    if os.path.exists(path):
        try:
            with open(path, 'rb') as h:
                head = h.read(len(BAK))
        except OSError as e:
            raise InputError('cannot read %s: %s' % (out, e.strerror))
        if head != BAK:
            raise InputError('%s is there and is not a backup of poolmgr: a backup replaces only an older backup '
                             '(.tfnbak); give another name' % out)
    return path


def replace_check(path, out, phrase, p, now):
    """An older backup in path goes only when the pass phrase of the new backup opens it, it is a backup of the pool
    p, it is not newer than p, and it has no part that the data folder does not have (now: files_of() of the data
    folder). Else InputError (a backup of another pool, for example a pool with the same name in another data folder;
    a backup with another pass phrase; a newer backup: a higher roster version or key epoch, or another key A after a
    recover; a backup with more parts: see lost_parts()): the option --replace replaces it."""
    if not os.path.exists(path):
        return
    try:
        files = unzip(open_(read(path), phrase)[1])
        other = pool_of(files)
    except (Failed, OSError) as e:
        raise InputError('%s is a backup that the new pass phrase does not open (%s): it can be the backup of another '
                         'pool. Give another name, or --replace replaces it' % (out, e))
    if other.pool_id != p.pool_id:
        raise InputError('%s is the backup of another pool (%s, ID %s): give another name, or --replace replaces it' %
                         (out, other.name, other.pool_id.hex()[:8]))
    newer = other.version > p.version or other.epoch > p.epoch
    other_a = other.admins[:1] != p.admins[:1]
    if newer or other_a:
        raise InputError('%s is a %s backup of the pool %s: the backup has roster version %d, epoch %d, key A %s; the '
                         'data folder has roster version %d, epoch %d, key A %s. Keep that backup (the data folder can '
                         'be an older copy): give another name, or --replace replaces it' % (
                             out, 'NEWER' if newer else 'DIFFERENT', p.name, other.version, other.epoch,
                             kid(other.admins[0]) if other.admins else 'none', p.version, p.epoch,
                             kid(p.admins[0]) if p.admins else 'none'))
    lost = lost_keys(files, now)
    if (other.version, other.epoch) == (p.version, p.epoch):
        lost += lost_parts(files, now)
    if lost:
        raise InputError('%s is a backup of the pool %s (roster version %d, epoch %d) with parts that the data folder '
                         'does not have (%s): the data folder can be an older copy. Keep that backup: give another '
                         'name, or --replace replaces it' % (out, p.name, other.version, other.epoch,
                                                            '; '.join(lost)))


def lost_keys(bak, now):
    """The key files (keys/*.pem) of a backup (bak) that the data folder (now) does not have, or has with other bytes,
    both {name: bytes}. The program never deletes a key file, so such a backup is never older at any roster version:
    for example "store key" on another computer adds keys/store_pool.pem and does not change the version."""
    out = []
    for n in sorted(bak):
        if n.startswith('keys/') and bak[n] != now.get(n):
            out.append('%s (%s)' % (BAK_WHAT.get(n, n), 'other bytes' if n in now else 'not there'))
    return out


def lost_parts(bak, now):
    """The parts of a backup (bak) that the data folder (now) does not have, both {name: bytes}: the files (for example
    keys/store_pool.pem of "store key") and the web passwords of boards that pool/webpass.json of the data folder does
    not have. Some commands (for example "store key") add a part and do not change the roster version or the epoch:
    then only these parts show a backup that is newer than the data folder. (A web password with another value does
    not show which side is newer: "webpass new" changes it in the data folder.) The key files: lost_keys()."""
    def norm(files):
        f = dict(files)
        if RECOVER_FILE in f:
            f.setdefault('pool/' + RECOVER_FILE, f.pop(RECOVER_FILE))
        return f
    bak, now = norm(bak), norm(now)
    out = [BAK_WHAT.get(n, n) for n in sorted(set(bak) - set(now)) if not n.startswith('keys/')]
    try:
        wb = json.loads(bak.get('pool/webpass.json', b'{}').decode('utf-8'))
        wn = json.loads(now.get('pool/webpass.json', b'{}').decode('utf-8'))
        miss = sorted(set(wb) - set(wn)) if isinstance(wb, dict) and isinstance(wn, dict) else []
    except (ValueError, UnicodeDecodeError):
        miss = []
    if miss and 'pool/webpass.json' in now:
        out.append('the web passwords of %d board(s) in pool/webpass.json: %s' % (len(miss), ', '.join(miss)))
    return out


def backup(data, out, phrase, replace=False):
    """The .tfnbak file of all of the pool (config.json, keys/*.pem, pool/*) in out: a new file, or an older backup
    of the same pool (backup_target(), replace_check(); replace: also another older backup)."""
    path = backup_target(out)
    if not replace:
        replace_check(path, out, phrase, data.need_pool(), files_of(data))
    with data.change():
        data.pool = None
        p = data.need_pool()
        files = files_of(data)
        if 'keys/admin_a.pem' not in files:
            raise InputError('no key A in %s: the backup would not reach the boards' % data.root)
        blob = seal(BAK, zip_of(files), phrase)
        unzip(open_(blob, phrase)[1])
        err = write_file(path, blob, private=True)
    data.log('backup written: pool %s, roster version %d, epoch %d, %d files' % (p.name, p.version, p.epoch,
                                                                                 len(files)))
    warn = pm_data.file_warn(out, err)
    return ['the backup %s: pool %s, roster version %d, epoch %d, %d files' % (out, p.name, p.version, p.epoch,
                                                                               len(files)),
            'keep it on a USB stick or another computer; poolmgr check-backup %s checks it' % out] + (
                [warn] if warn else [])


# ---------------------------------------------------------------- the checks
def pool_of(files):
    """The pool.Pool of the files of a backup (the fields of pool.Pool.load, from memory). Failed for a fault."""
    try:
        j = json.loads(files['pool/pool.json'].decode('utf-8'))
        s = json.loads(files['pool/secrets.json'].decode('utf-8'))
        p = pool.Pool()
        p.pool_id, p.name, p.version, p.epoch = bytes.fromhex(j['pool']), j['name'], j['v'], j['epoch']
        p.flags, p.admins, p.lan, p.vpn = j['flags'], [bytes.fromhex(a) for a in j['admins']], j['lan'], j['vpn']
        p.members = j['members']
        p.pool_k, p.prev, p.wks = bytes.fromhex(s['pool_k']), s['prev'], s['wk']
        p.roster_file = files.get('pool/roster.bin', b'')
    except KeyError as e:
        raise Failed('the pool part of the backup has no %s' % e)
    except (ValueError, TypeError, AttributeError) as e:
        raise Failed('the pool part of the backup does not load (%s)' % e)
    return p


def check_bak(files, data):
    lines = []
    try:
        cfg = json.loads(files.get('config.json', b'{"format": 1}').decode('utf-8'))
    except ValueError:
        raise Failed('config.json of the backup is not JSON')
    if cfg.get('format') != 1:
        raise Failed('config.json of the backup has the format %r' % cfg.get('format'))
    p = pool_of(files)
    lines.append('the pool %s loads: ID %s, roster version %d, epoch %d, %d members' % (
        p.name, p.pool_id.hex(), p.version, p.epoch, len(p.members)))
    if p.roster_file:
        try:
            r = pool.roster_parse(p.roster_file)
        except pool.RosterError as e:
            raise Failed('roster.bin of the backup is damaged (%s)' % e.why)
        if r.pool_id != p.pool_id or pool.roster_signer(r) not in p.admins:
            raise Failed('roster.bin of the backup is not a roster of this pool signed by its admin key')
        lines.append('roster.bin: version %d, signed by the admin key %s' % (r.version, r.kid.hex()))
    pem = files.get('keys/admin_a.pem')
    if pem is None:
        raise Failed('the backup has no key A')
    a = pub_of(pem)
    if not p.admins or a != p.admins[0]:
        raise Failed('key A of the backup (key ID %s) is not the admin key A of the pool' % kid(a))
    if not key_check(pem, a):
        raise Failed('key A of the backup does not sign')
    lines.append('key A (key ID %s) signs a test text and verifies' % kid(a))
    sp = files.get('keys/store_pool.pem')
    if sp is not None:
        sp_pub = pub_of(sp)
        if not key_check(sp, sp_pub):
            raise Failed('the pool store key of the backup does not sign')
        lines.append('the pool store key (key ID %s) signs a test text and verifies' % kid(sp_pub))
    boards = pool_tool.boards(p)
    miss = [m['name'] for m in boards if m['id'] not in p.wks]
    if miss:
        raise Failed('the backup has no request key (wk) of %s' % ', '.join(miss))
    lines.append('a request key (wk) of each of the %d boards' % len(boards))
    if len(p.admins) > 1:
        lines.append('key R of the pool: key ID %s (not in the backup: its .tfnkey file)' % kid(p.admins[1]))
    now = data.load() if data is not None and data.has_pool() else None
    if now is not None:
        if now.pool_id != p.pool_id:
            lines.append('NOTE: the data folder has another pool (%s)' % now.name)
            return lines
        diff = differences(files, files_of(data), p, now)
        lines += ['BACKUP IS OLD: %s' % d for d in diff]
        lines.append('BACKUP IS OLD: make a new backup (poolmgr backup <file>)' if diff else
                     'the backup has the state of the data folder (each file is the same)')
    return lines


def same_json(a, b):
    try:
        return json.loads(a.decode('utf-8')) == json.loads(b.decode('utf-8'))
    except (ValueError, UnicodeDecodeError):
        return False


def differences(bak, now, p_bak, p_now):
    """The differences of the files of a backup (bak) and the files of the data folder (now), both {name: bytes}:
    one text for each part that is not the same ([] = the same)."""
    out = []
    for name in sorted(set(bak) | set(now)):
        a, b = bak.get(name), now.get(name)
        what = BAK_WHAT.get(name, name)
        if a == b or (name.endswith('.json') and a is not None and b is not None and same_json(a, b) and
                      name != 'config.json'):
            continue
        if name == 'config.json':
            out += cfg_diff(a or b'{}', b or b'{}')
        elif a is None:
            out.append('%s: only in the data folder' % what)
        elif b is None:
            out.append('%s: only in the backup' % what)
        elif name == 'pool/pool.json':
            out.append('%s: the backup has roster version %d, epoch %d, %d members; the data folder %d, %d, %d' % (
                what, p_bak.version, p_bak.epoch, len(pool_tool.boards(p_bak)), p_now.version, p_now.epoch,
                len(pool_tool.boards(p_now))))
        else:
            out.append('%s: not the same' % what)
    return out


def cfg_diff(a, b):
    """The values of config.json of the backup (a) and of the data folder (b) that are not the same."""
    try:
        ca = dict(pm_data.DEFAULT_CFG, **json.loads(a.decode('utf-8')))
        cb = dict(pm_data.DEFAULT_CFG, **json.loads(b.decode('utf-8')))
    except (ValueError, UnicodeDecodeError, TypeError):
        return ['config.json: not the same']
    out = []
    for k in sorted(set(ca) | set(cb)):
        if k in CFG_SKIP or ca.get(k) == cb.get(k):
            continue
        va, vb = ca.get(k), cb.get(k)
        if k == 'channel' or isinstance(va, (dict, list)) or isinstance(vb, (dict, list)):
            out.append('config.json: %s is not the same' % CFG_WHAT.get(k, k))
        else:
            out.append('config.json: %s (the backup: %s, the data folder: %s)' % (CFG_WHAT.get(k, k), va, vb))
    return out


def check_key(pem, data):
    try:
        pub = pub_of(pem)
    except fw_sign.SignError:
        raise Failed('the key file has no good P-256 key')
    if not key_check(pem, pub):
        raise Failed('the key in the key file does not sign')
    lines = ['the key (key ID %s) signs a test text and verifies' % kid(pub)]
    p = data.load() if data is not None and data.has_pool() else None
    if p is None:
        lines.append('no pool in the data folder: the admin check of the key is not done')
    elif pub not in p.admins[1:]:
        raise Failed('the key (key ID %s) is not the recovery key of the pool %s' % (kid(pub), p.name))
    else:
        lines.append('it is the recovery key R of the pool %s' % p.name)
    return lines


def check(blob, phrase, data=None):
    """The check of a .tfnbak or .tfnkey file: the result lines; Failed when a part is bad."""
    kind, plain = open_(blob, phrase)
    lines = ['the file opens with the pass phrase (%s)' % kind.decode()]
    if kind == KEY:
        lines += check_key(plain, data)
    else:
        lines += check_bak(unzip(plain), data)
    lines.append('CHECK OK')
    return lines


# ---------------------------------------------------------------- restore
def restore_check(root):
    """The folder root can take a restore (it is not there, it is empty, or it has only the empty parts of a data
    folder with no pool and the change lock): its full path; else InputError."""
    root = os.path.abspath(os.path.expanduser(root))
    if os.path.exists(root) and not os.path.isdir(root):
        raise InputError('%s is a file, not a folder' % root)
    bad = pm_data.leftovers(root) if os.path.isdir(root) else []
    if bad:
        raise InputError('%s is not an empty folder (it has %s): a restore goes only into an empty data folder, or a '
                         'data folder of poolmgr with no pool' % (root, ', '.join(bad[:6])))
    return root


def restore(blob, root, phrase):
    """The backup into the data folder root: a new folder, an empty folder, or a data folder with no pool (only
    empty parts and the change lock). The files go into a temporary folder in root first; then they move into place
    (pool/ last). A fault removes only what this restore made."""
    root = restore_check(root)
    kind, plain = open_(blob, phrase)
    if kind != BAK:
        raise InputError('this is a key file (.tfnkey), not a backup (.tfnbak)')
    files = unzip(plain)
    lines = check_bak(files, None)
    if RECOVER_FILE in files and 'pool/' + RECOVER_FILE not in files:
        # (the state of recover of an older poolmgr goes into pool/, which has its own rights)
        files['pool/' + RECOVER_FILE] = files.pop(RECOVER_FILE)
    data = Data(root)
    made = data.make_root()
    try:
        with data.change(new=True):
            restore_check(root)
            tmp = pm_data.private_tmp(root, '.restore-')
            try:
                for name, body in sorted(files.items()):
                    path = os.path.join(tmp, *name.split('/'))
                    os.makedirs(os.path.dirname(path), mode=0o700, exist_ok=True)
                    write_file(path, body, 0o600)
                pm_data.move_parts(root, tmp, PARTS)
            finally:
                if not pm_data.remove_tree(tmp):
                    # (at once: a fault of the restore discards the lines)
                    say('NOTE: the temporary folder %s stays (delete it: it has copies of the keys)' % tmp)
    except BaseException:
        if made:
            pm_data.drop_root(root)
        raise
    data.make()
    p = data.load()
    data.log('restore: pool %s, roster version %d, epoch %d, %d files' % (p.name, p.version, p.epoch, len(files)))
    if os.path.isfile(os.path.join(root, 'service', 'tfn-poolmgr-task.xml')):
        lines.append('NOTE: the task file of an older "service install" is in %s: poolmgr service install again '
                     'starts serve for this pool' % os.path.join(root, 'service'))
    lines += ['WARN: ' + ln for ln in pm_data.rights_lines(root)]
    return lines + ['restored into %s: %d files' % (root, len(files)),
                    'now: poolmgr list --live (each member must answer)']


# ---------------------------------------------------------------- recover
class OldMid:
    """The pool view of pool_tool.Board for the header of the OLD manager ID."""

    def __init__(self, mid):
        self._mid = mid

    def mid(self):
        return self._mid


def state_file(data):
    """pool/recover.json: the folder pool/ has its own rights (the state has old request keys)."""
    return os.path.join(data.pool_dir, RECOVER_FILE)


RECOVER_FORM = ('{"format": 1, "old_mid": "<hex>", "new_mid": "<hex>", "old_wk": {"<device ID>": "<hex>"}, '
                '"pending": ["<device ID>"], "old_mid_of": {"<device ID>": "<hex>"} (optional)}')


def load_state(data):
    """The state of the last recover: pool/recover.json, else recover.json at the top of the data folder (an older
    poolmgr, or the restore of its backup). None when no board waits. A damaged file stops the program (damaged(),
    exit 2): it has the only copy of the old request keys of the boards that wait, so recover does not write over it."""
    for path in (state_file(data), os.path.join(data.root, RECOVER_FILE)):
        st = pm_data.read_strict(path, None, dict, RECOVER_FORM)
        if st is None:
            continue
        if not st.get('pending'):
            return None
        check_state(path, st)
        return st
    return None


def check_state(path, st):
    """A state with boards that wait needs old_mid, new_mid (hex) and old_wk ({device ID: hex}); old_mid_of
    ({device ID: hex}) is optional: else damaged()."""
    try:
        by = st.get('old_mid_of', {})
        ok = (isinstance(st['pending'], list) and all(isinstance(i, str) for i in st['pending']) and
              isinstance(st['old_wk'], dict) and all(isinstance(v, str) for v in st['old_wk'].values()) and
              isinstance(by, dict) and all(isinstance(v, str) for v in by.values()))
        if ok:
            for v in [st['old_mid'], st['new_mid']] + list(st['old_wk'].values()) + list(by.values()):
                bytes.fromhex(v)
    except (KeyError, TypeError, ValueError):
        ok = False
    if not ok:
        raise pm_data.damaged(path, 'boards wait, but old_mid, new_mid or old_wk is not there or not hex: the form '
                              '%s' % RECOVER_FORM)


def save_state(data, st):
    """Write pool/recover.json (0600; on Windows the rights of pool/). A recover.json at the top of the data folder
    (an older poolmgr) goes."""
    write_file(state_file(data), (json.dumps(st, indent=1, sort_keys=True) + '\n').encode(), 0o600)
    old = os.path.join(data.root, RECOVER_FILE)
    if os.path.isfile(old):
        try:
            pm_data.retry(lambda: os.remove(old))
        except OSError as e:
            say('NOTE: %s stays (%s): delete it, it has old request keys of the boards' % (old, e.strerror))


def sign_roster_r(p, r_pem, r_pub):
    """A new roster version signed by key R (the file bytes, also kept in p.roster_file)."""
    p.version += 1
    body = pool.roster_body(p.roster())
    data = body + pool.sig_record(r_pub, sign_mem(r_pem, body))
    if len(data) > pool.ROSTER_MAX:
        raise Failed('the roster has more than %d bytes' % pool.ROSTER_MAX)
    r = pool.roster_parse(data)
    if pool.roster_signer(r) != r_pub:
        raise Failed('the roster signature of key R does not verify')
    p.roster_file = data
    return data


def member_ip(data, m):
    """The address of a member: pm_board.address (addrs.json, pool.json or its beacon), else ''."""
    try:
        return pm_board.address(data, m['id'])
    except (Failed, InputError, pool_tool.ToolError, OSError):
        return m.get('ip') or ''


def send_one(data, p, m, old_mid, old_wk, wp):
    """The R-signed roster and the new keys to one board (design 2.12), only when the board at the address gives the
    device ID of m (pool_tool.Board dev_id). Returns the answer of op=key."""
    ip = member_ip(data, m)
    if not ip:
        raise pool_tool.ToolError('no address')
    old = pool_tool.Board(ip, OldMid(old_mid), old_wk, wp, dev_id=m['id'])
    try:
        old.signed_post('set', {'roster': p.roster_file.hex()})
    except pool_tool.Refused as e:
        if e.code != 401:  # (401: the board has the new roster already, from an earlier try)
            raise
    body = pool.key_body(bytes.fromhex(m['bx']), bytes.fromhex(m['id']), p.roster_file, p.epoch, p.pool_k,
                         p.wk(m['id']))
    try:
        return pool_tool.Board(ip, p, old_wk, wp, dev_id=m['id']).signed_post('key', body)
    except pool_tool.Refused as e:
        if e.code != 401:
            raise
    # (the board has the new key already, from an earlier try)
    return pool_tool.Board(ip, p, p.wk(m['id']), wp, dev_id=m['id']).signed_post('key', body)


def recover(data, recovery_blob, phrase, new_key=False):
    """A new key A2 signed by R, a new manager member, a new pool key and new wk values (design 2.12). When boards of
    the last recover wait (pool/recover.json), recover sends to them again and key A does not change (a WARN line
    tells it); new_key: a new key A also then (the computer with the key A of the last recover is lost or out). The
    boards that wait keep their old manager ID and old request key in the state (old_mid_of), so a later recover still
    reaches them."""
    kind, r_pem = open_(recovery_blob, phrase)
    if kind != KEY:
        raise InputError('give the recovery key file (.tfnkey), not a backup')
    r_pub = pub_of(r_pem)
    with data.change():
        data.pool = None
        p = data.need_pool()
        if r_pub not in p.admins[1:]:
            raise Failed('the key (key ID %s) is not the recovery key of the pool %s' % (kid(r_pub), p.name))
        st = load_state(data)
        if st is not None and st.get('new_mid') != p.mid().hex():
            st = None
        if st is not None and not new_key:
            print('recover: the boards of the last recover that did not answer (%d)' % len(st['pending']))
            print('WARN: key A did NOT change (key A %s of the last recover). When the computer with that key A is '
                  'lost, stolen or out of your control: poolmgr recover --new-key' % kid(p.admins[0]))
        else:
            st = new_admin(data, p, r_pem, r_pub, st)
        wps = pool_tool.load_webpass(data.pool_dir)
        done, bad = [], []
        for m in pool_tool.boards(p):
            if m['id'] not in st['pending']:
                continue
            try:
                old_mid = bytes.fromhex(st.get('old_mid_of', {}).get(m['id'], st['old_mid']))
                ans = send_one(data, p, m, old_mid, bytes.fromhex(st['old_wk'][m['id']]), wps.get(m['mac']))
                done.append(m['id'])
                print('  %s: the new roster and key (epoch %s)' % (m['name'], ans.get('epoch', p.epoch)))
            except (pool_tool.Refused, pool_tool.ToolError, KeyError, ValueError) as e:
                bad.append(m['name'])
                print('  %s: NOT REACHED: %s' % (m['name'], e))
        left = [i for i in st['pending'] if i not in done and p.find(i) is not None]
        if left:
            st_left = {'format': 1, 'old_mid': st['old_mid'], 'new_mid': st['new_mid'], 'pending': left,
                       'old_wk': {i: st['old_wk'][i] for i in left if i in st['old_wk']}}
            by = {i: v for i, v in st.get('old_mid_of', {}).items() if i in left}
            if by:
                st_left['old_mid_of'] = by
            save_state(data, st_left)
        else:
            save_state(data, {'format': 1, 'pending': []})
    data.log('recover: key A %s, roster version %d, epoch %d, %d boards done, %d not reached' % (
        kid(p.admins[0]), p.version, p.epoch, len(done), len(left)))
    if bad:
        raise Failed('recover: %d board(s) not reached (%s): when they are on, run "poolmgr recover" again. '
                     'BACKUP IS OLD: poolmgr backup <file>' % (len(bad), ', '.join(bad)))
    return ['recover done: key A %s, roster version %d, epoch %d, %d boards' % (kid(p.admins[0]), p.version,
                                                                                  p.epoch, len(done)),
            'BACKUP IS OLD: poolmgr backup <file>']


def new_admin(data, p, r_pem, r_pub, last=None):
    """The new key A2, the R-signed roster, the new pool key and wk values: saved (key A2, the pool, recover.json).
    last: the state of the last recover when boards of it wait: these boards keep its old manager ID and old request
    key (old_mid_of, old_wk). Returns the state of recover.json."""
    old_mid = p.mid()
    boards = pool_tool.boards(p)
    old_wk = {m['id']: p.wks[m['id']] for m in boards if m['id'] in p.wks}
    old_mid_of = {}
    for i in last['pending'] if last else ():
        if i in old_wk and i in last['old_wk']:
            old_wk[i] = last['old_wk'][i]
            old_mid_of[i] = last.get('old_mid_of', {}).get(i, last['old_mid'])
    miss = [m['name'] for m in boards if m['id'] not in old_wk]
    if miss:
        raise Failed('no request key (wk) of %s: these boards need LEAVE and a new adopt' % ', '.join(miss))
    a2_pem = new_pem()
    a2 = pub_of(a2_pem)
    mgrs = [m for m in p.members if m['role'] == pool.ROLE_MANAGER and m['mac'] == pool.MANAGER_MAC.hex()]
    name = mgrs[0]['name'] if mgrs else 'MANAGER'
    for m in mgrs:
        p.remove(m['id'])
    p.admins = [a2, r_pub]
    p.add_manager(name)
    p.new_key()
    for m in boards:
        p.wks[m['id']] = pc.rand(32).hex()
    sign_roster_r(p, r_pem, r_pub)
    st = {'format': 1, 'old_mid': old_mid.hex(), 'new_mid': p.mid().hex(), 'old_wk': old_wk,
          'pending': [m['id'] for m in boards], 't': int(time.time())}
    if old_mid_of:
        st['old_mid_of'] = old_mid_of
    data.make()
    data.private_parts()
    save_state(data, st)
    write_file(data.key_a, a2_pem, 0o600)
    p.save(data.pool_dir)
    data.log('recover: the new key A %s, roster version %d signed by key R %s, epoch %d' % (
        kid(a2), p.version, kid(r_pub), p.epoch))
    print('recover: the new key A %s, roster version %d, epoch %d; to %d boards:' % (kid(a2), p.version, p.epoch,
                                                                                    len(boards)))
    return st
