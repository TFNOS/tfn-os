#!/usr/bin/env python3
"""The test of poolmgr/pm_keys.py (P6 design 2.12 and 3.1): key A, key R, the encrypted files .tfnkey and .tfnbak,
backup, check-backup, restore and recover, against FAKE boards (tests/unit/poolmgr_fake.py). No real board and no
network. All keys are TEST keys that the test makes in a temporary folder.

The steps: the .tfnkey and .tfnbak round trip with both KDFs (scrypt and PBKDF2); a wrong pass phrase; one changed
byte in each part of a file; key A (0600, never overwritten); key R (only the encrypted file: no clear copy of the
key in the data folder); the check of a key file and of a backup; a pool of 3 fake boards (the JOIN of pool_tool.py);
backup; restore into a new folder (and the refusal of a folder that is not empty); push from the restored folder;
recover with R while one board is off (the old header first, then the new one; the old wk = 401 after it; a roster of
the old key A is refused); recover again for the board that was off (the state in pool/recover.json; a recover.json
at the top of an older data folder is read and goes); push with the new key A; no key value, pool key or wk in the
log or in the output; a fault in write_new leaves no partial file; a fault in write_file leaves no temporary file;
backup replaces only an older backup of the same pool with the same pass phrase, and not a newer backup or a backup
with another key A (else --replace); the NOTE of a temporary folder of a failed restore comes at once.

Usage: python tests/unit/test_poolmgr_keys.py
"""
import contextlib
import io
import json
import os
import shutil
import stat
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
PM = os.path.join(ROOT, 'poolmgr')
sys.path.insert(0, HERE)
sys.path.insert(0, PM)
import pm_data  # noqa: E402
import pm_keys  # noqa: E402
import poolmgr  # noqa: E402
from pm_shared import Failed, InputError, fw_sign, pool, pool_crypto as pc, pool_tool  # noqa: E402
from poolmgr_fake import FakeBoard, FakeConsole  # noqa: E402

FAILS = []
CHECKS = [0]
PHRASE = 'a test pass phrase 1'
OUT = io.StringIO()  # all output of the functions under test (the test looks for secrets in it)
POSIX = os.name == 'posix'


def expect(cond, what):
    CHECKS[0] += 1
    if not cond:
        FAILS.append(what)
        print('FAIL', what)


def call(fn, *a):
    """fn(*a) with its output caught into OUT: (the result or the exception, the output)."""
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf), contextlib.redirect_stderr(buf):
        try:
            r = fn(*a)
        except (Failed, InputError, pool_tool.Refused, pool_tool.ToolError) as e:
            r = e
    OUT.write(buf.getvalue())
    return r, buf.getvalue()


def tool(*argv):
    return call(pool_tool.main, list(argv))


def raises(r, cls):
    return isinstance(r, cls)


def mode(path):
    return stat.S_IMODE(os.stat(path).st_mode)


def files_under(d):
    for base, _, names in os.walk(d):
        for n in names:
            yield os.path.join(base, n)


def same_pool(p, b):
    return (b.roster is not None and b.roster.pool_id == p.pool_id and b.roster.version == p.version and
            b.pool_k == p.pool_k and b.epoch == p.epoch and b.wk == p.wk(b.id.hex()))


def raw_post(board, path, body, hdr):
    req = urllib.request.Request('http://%s%s' % (board.ip, path), data=json.dumps(body).encode(), method='POST')
    req.add_header(pool.HDR, hdr)
    try:
        with urllib.request.urlopen(req, timeout=5) as r:
            return r.status
    except urllib.error.HTTPError as e:
        return e.code


# ---------------------------------------------------------------- the encrypted files
def files_part(d):
    for prefer, kdf in ((None, pm_keys.KDF_SCRYPT if pm_keys.has_scrypt() else pm_keys.KDF_PBKDF2),
                        ('pbkdf2', pm_keys.KDF_PBKDF2)):
        pm_keys.KDF_PREFER = prefer
        for kind in pm_keys.KINDS:
            plain = os.urandom(300) + b'\n\r\n' + kind
            blob = pm_keys.seal(kind, plain, PHRASE)
            tag = '%s %s' % (kind.decode(), 'scrypt' if kdf == pm_keys.KDF_SCRYPT else 'pbkdf2')
            expect(blob[:7] == kind and blob[7] == kdf, '%s: the header' % tag)
            expect(len(blob) == pm_keys.HEAD + len(plain) + pm_keys.TAG, '%s: the length' % tag)
            expect(pm_keys.open_(blob, PHRASE) == (kind, plain), '%s: the round trip' % tag)
            expect(raises(call(pm_keys.open_, blob, PHRASE + 'x')[0], Failed), '%s: a wrong pass phrase' % tag)
            # one changed byte in each part: the magic, the KDF id, the salt, the KDF values, the nonce, the cipher
            # text, the tag
            for i in (0, 7, 8, 8 + 16, pm_keys.HEAD - 1, pm_keys.HEAD + 5, len(blob) - 1):
                bad = bytearray(blob)
                bad[i] ^= 0x01
                expect(raises(call(pm_keys.open_, bytes(bad), PHRASE)[0], Failed), '%s: byte %d changed' % (tag, i))
            expect(raises(call(pm_keys.open_, blob[:-1], PHRASE)[0], Failed), '%s: a cut file' % tag)
        expect(pm_keys.seal(pm_keys.KEY, b'x', PHRASE) != pm_keys.seal(pm_keys.KEY, b'x', PHRASE),
               'a new salt and nonce each time')
    pm_keys.KDF_PREFER = None
    expect(raises(call(pm_keys.seal, pm_keys.KEY, b'x', 'short')[0], InputError), 'a short pass phrase')
    expect(raises(call(pm_keys.open_, b'TFNBAK1', PHRASE)[0], Failed), 'a file of 7 bytes')
    expect(raises(call(pm_keys.open_, b'NOTAKEY' + bytes(80), PHRASE)[0], Failed), 'another magic')
    # a KDF with values that the program does not take (a file that asks for too much memory)
    head = b'TFNKEY1' + bytes([pm_keys.KDF_SCRYPT]) + bytes(16) + bytes([24, 8, 1, 0]) + bytes(12)
    expect(raises(call(pm_keys.open_, head + bytes(32), PHRASE)[0], Failed), 'scrypt N 2^24 refused')
    head = b'TFNKEY1' + bytes([9]) + bytes(32)
    expect(raises(call(pm_keys.open_, head + bytes(32), PHRASE)[0], Failed), 'an unknown KDF refused')

    # a signature with a key in memory: the temporary copy is encrypted
    pem = pm_keys.new_pem()
    pub = pm_keys.pub_of(pem)
    enc = pm_keys.enc_pem(pem, 'k' * 64)
    expect(b'ENCRYPTED PRIVATE KEY' in enc and pem.split(b'\n')[1] not in enc, 'the temporary key copy is encrypted')
    expect(pm_keys.key_check(pem, pub), 'sign_mem: a good signature')
    other = pm_keys.pub_of(pm_keys.new_pem())
    expect(not pm_keys.key_check(pem, other), 'sign_mem: another key does not verify')


# ---------------------------------------------------------------- the keys, the pool, the backup, recover
def pool_part(d):
    data = pm_data.Data(os.path.join(d, 'data'))
    a = pm_keys.make_admin(data)
    expect(len(a) == 65 and os.path.isfile(data.key_a), 'make_admin: key A')
    expect(raises(call(pm_keys.make_admin, data)[0], InputError), 'make_admin: key A is never overwritten')
    rfile = os.path.join(d, 'TESTPOOL-recovery.tfnkey')
    r = pm_keys.make_recovery(data, rfile, PHRASE)
    expect(len(r) == 65 and r != a, 'make_recovery: key R')
    expect(raises(call(pm_keys.make_recovery, data, rfile, PHRASE)[0], InputError),
           'make_recovery: a key file is never overwritten')
    rblob = open(rfile, 'rb').read()
    r_pem = pm_keys.open_(rblob, PHRASE)[1]
    body = b''.join(r_pem.split(b'\n')[1:-2])
    for f in list(files_under(data.root)) + [rfile]:
        expect(body not in open(f, 'rb').read().replace(b'\r', b'').replace(b'\n', b''),
               'key R is not in clear in %s' % os.path.basename(f))
    res, out = call(pm_keys.check, rblob, PHRASE, data)
    expect(isinstance(res, list) and res[-1] == 'CHECK OK' and 'not done' in ' '.join(res),
           'check of key R with no pool: %s' % res)
    if POSIX:
        expect(mode(data.key_a) == 0o600 and mode(rfile) == 0o600, 'the modes of the key files: 0600')
        expect(mode(data.root) == 0o700 and mode(os.path.dirname(data.key_a)) == 0o700, 'the folders: 0700')

    # the pool (as init does it) and 3 fake boards that join with the console
    p = pool.Pool.create('TESTPOOL', a, r)
    p.add_manager()
    p.sign(data.key_a)
    p.save(data.pool_dir)
    data.save_cfg()
    boards = [FakeBoard('k%d' % i, bytes.fromhex('0200000000a%d' % i), d) for i in range(3)]
    cons = {'k%d' % i: b for i, b in enumerate(boards)}
    pool_tool.open_console = lambda port: FakeConsole(cons[port])
    pool_tool.ALLOW_WAIT = 8
    base = ['--dir', data.pool_dir, '--key', data.key_a]
    for i, b in enumerate(boards):
        code, out = tool(*base, 'adopt', b.ip, '--console', 'k%d' % i, '--name', 'K%d' % i)
        expect(code == 0, 'adopt K%d: %s' % (i, out[-300:]))
    code, out = tool(*base, 'webpass')
    expect(code == 0 and all(b.webpw for b in boards), 'web passwords on the boards')
    p = pool.Pool.load(data.pool_dir)
    expect(all(same_pool(p, b) for b in boards), 'the 3 boards in the pool')

    res, out = call(pm_keys.check, rblob, PHRASE, data)
    expect(isinstance(res, list) and 'recovery key R' in ' '.join(res), 'check of key R: an admin key of the pool')
    wrong = os.path.join(d, 'wrong.tfnkey')
    pm_keys.make_recovery(data, wrong, PHRASE)
    expect(raises(call(pm_keys.check, open(wrong, 'rb').read(), PHRASE, data)[0], Failed),
           'check of a key that is not key R of the pool')

    # backup and its check
    bfile = os.path.join(d, 'TESTPOOL-backup.tfnbak')
    res, out = call(pm_keys.backup, data, bfile, PHRASE)
    expect(isinstance(res, list) and os.path.isfile(bfile), 'backup: %s' % res)
    bblob = open(bfile, 'rb').read()
    expect(bblob[:7] == b'TFNBAK1', 'backup: the magic')
    if POSIX:
        expect(mode(bfile) == 0o600, 'the mode of the backup: 0600')
    res, out = call(pm_keys.check, bblob, PHRASE, data)
    text = ' '.join(res) if isinstance(res, list) else str(res)
    expect(isinstance(res, list) and res[-1] == 'CHECK OK' and 'the state of the data folder' in text and
           'key A' in text and '3 boards' in text, 'check of the backup: %s' % text)
    expect(raises(call(pm_keys.check, bblob, PHRASE + 'x', data)[0], Failed), 'check: a wrong pass phrase')
    bad = bytearray(bblob)
    bad[len(bad) // 2] ^= 0x40
    expect(raises(call(pm_keys.check, bytes(bad), PHRASE, data)[0], Failed), 'check: a changed byte')
    # a backup with a strange name, or a key A that is not the admin key: refused
    plain = pm_keys.unzip(pm_keys.open_(bblob, PHRASE)[1])
    evil = dict(plain)
    evil['../evil.txt'] = b'x'
    expect(raises(call(pm_keys.check, pm_keys.seal(pm_keys.BAK, pm_keys.zip_of(evil), PHRASE), PHRASE)[0], Failed),
           'check: a name out of the data folder')
    other = dict(plain)
    other['keys/admin_a.pem'] = pm_keys.new_pem()
    expect(raises(call(pm_keys.check, pm_keys.seal(pm_keys.BAK, pm_keys.zip_of(other), PHRASE), PHRASE)[0], Failed),
           'check: a key A that is not the admin key of the pool')
    expect(raises(call(pm_keys.restore, rblob, os.path.join(d, 'r0'), PHRASE)[0], InputError),
           'restore: a key file is not a backup')

    # restore: only into an empty folder; then push from the restored folder
    expect(raises(call(pm_keys.restore, bblob, data.root, PHRASE)[0], InputError), 'restore: a folder with a pool')
    full = os.path.join(d, 'full')
    os.makedirs(full)
    open(os.path.join(full, 'x'), 'wb').close()
    expect(raises(call(pm_keys.restore, bblob, full, PHRASE)[0], InputError), 'restore: a folder that is not empty')
    root2 = os.path.join(d, 'restored')
    res, out = call(pm_keys.restore, bblob, root2, PHRASE)
    expect(isinstance(res, list), 'restore: %s' % res)
    data2 = pm_data.Data(root2)
    p2 = data2.load()
    expect(p2 is not None and p2.pool_id == p.pool_id and p2.wks == p.wks and p2.pool_k == p.pool_k,
           'restore: the same pool')
    for name in ('config.json', 'keys/admin_a.pem', 'pool/pool.json', 'pool/secrets.json', 'pool/roster.bin',
                 'pool/webpass.json'):
        expect(open(os.path.join(data.root, name), 'rb').read() == open(os.path.join(root2, name), 'rb').read(),
               'restore: %s is the same' % name)
    if POSIX:
        expect(mode(data2.key_a) == 0o600 and mode(os.path.join(root2, 'pool', 'secrets.json')) == 0o600,
               'restore: the secret files 0600')
    check_compare(bblob, data2)
    code, out = tool('--dir', data2.pool_dir, '--key', data2.key_a, 'push')
    expect(code == 0, 'push from the restored folder: %s' % out[-300:])
    p = pool.Pool.load(data2.pool_dir)
    expect(all(same_pool(p, b) for b in boards), 'the boards after the push from the restored folder')

    # recover in the restored folder (the computer was stolen): K2 is off
    old = pool.Pool.load(data2.pool_dir)
    old_a, old_mid = data2.key_a + '.thief', old.mid()
    with open(old_a, 'wb') as h:
        h.write(open(data2.key_a, 'rb').read())
    k2 = old.find('K2')
    k2_ip = k2['ip']
    k2['ip'] = '127.0.0.1:1'
    old.save(data2.pool_dir)
    res, out = call(pm_keys.recover, data2, bblob, PHRASE)
    expect(raises(res, InputError), 'recover: a backup is not a key file')
    res, out = call(pm_keys.recover, data2, open(wrong, 'rb').read(), PHRASE)
    expect(raises(res, Failed), 'recover: a key that is not key R')
    res, out = call(pm_keys.recover, data2, rblob, PHRASE)
    expect(raises(res, Failed) and 'K2' in str(res) and 'NOT REACHED' in out, 'recover: K2 not reached: %s' % res)
    p = pool.Pool.load(data2.pool_dir)
    expect(p.admins == [pc.p256_pub(data2.key_a), r] and p.admins[0] != a, 'recover: the admins = [A2, R]')
    expect(p.epoch == old.epoch + 1 and p.pool_k != old.pool_k and p.version > old.version,
           'recover: a new epoch and pool key, a new version')
    mgr = [m for m in p.members if m['role'] == pool.ROLE_MANAGER]
    expect(len(mgr) == 1 and mgr[0]['id'] == p.mid().hex() and mgr[0]['mac'] == pool.MANAGER_MAC.hex(),
           'recover: the new manager member')
    r_sig = pool.roster_parse(p.roster_file)
    expect(pool.roster_signer(r_sig) == r, 'recover: key R signs the roster')
    expect(same_pool(p, boards[0]) and same_pool(p, boards[1]), 'recover: K0 and K1 have the new roster and keys')
    expect(boards[2].epoch == old.epoch and boards[2].roster.version == old.version, 'recover: K2 has the old state')
    expect(all(p.wks[b.id.hex()] != old.wks[b.id.hex()] for b in boards), 'recover: a new wk for each board')
    state = os.path.join(root2, 'pool', 'recover.json')
    st = json.loads(open(state, 'rb').read())
    expect(st.get('pending') == [boards[2].id.hex()] and list(st.get('old_wk', {})) == [boards[2].id.hex()],
           'pool/recover.json: only K2 waits')
    expect(not os.path.exists(os.path.join(root2, 'recover.json')), 'no recover.json at the top of the data folder')
    if POSIX:
        expect(mode(state) == 0o600, 'pool/recover.json 0600')
    # a backup with the state of recover at the top (an older poolmgr): restore puts it into pool/
    files = pm_keys.files_of(data2)
    files['recover.json'] = files.pop('pool/recover.json')
    oblob = pm_keys.seal(pm_keys.BAK, pm_keys.zip_of(files), PHRASE)
    root3 = os.path.join(d, 'restored_top_state')
    res, out = call(pm_keys.restore, oblob, root3, PHRASE)
    expect(isinstance(res, list) and open(os.path.join(root3, 'pool', 'recover.json'), 'rb').read() ==
           open(state, 'rb').read() and not os.path.exists(os.path.join(root3, 'recover.json')),
           'restore of a backup with recover.json at the top: pool/recover.json (%s)' % (res,))

    # the thief: the old wk and the old manager ID get 401; a roster of the old key A is refused
    for b in boards[:2]:
        hdr = pool.web_header(old.wk(b.id.hex()), b.bn, old_mid, pool_tool.COUNTER.next(), 'POST',
                              '/api/pool?op=webpass')
        expect(raw_post(b, '/api/pool?op=webpass', {'pw': 'x'}, hdr) == 401, 'the old wk gets 401 on %s' % b.name)
        hdr = pool.web_header(old.wk(b.id.hex()), b.bn, p.mid(), pool_tool.COUNTER.next(), 'POST',
                              '/api/pool?op=webpass')
        expect(raw_post(b, '/api/pool?op=webpass', {'pw': 'x'}, hdr) == 401,
               'the old wk with the new manager ID gets 401 on %s' % b.name)
        # (the control: the new wk and the new manager ID pass the same gate)
        hdr = pool.web_header(p.wk(b.id.hex()), b.bn, p.mid(), pool_tool.COUNTER.next(), 'POST',
                              '/api/pool?op=webpass')
        expect(raw_post(b, '/api/pool?op=webpass', {'pw': b.webpw}, hdr) == 200,
               'the new wk and the new manager ID pass on %s' % b.name)
    thief = pool.Pool.load(data2.pool_dir)
    thief.admins = [old.admins[0], r]
    thief.version += 1
    forged = pool.roster_sign(thief.roster(), old_a)
    for b in boards[:2]:
        expect(pool.roster_accept(b.roster, forged, b.id, b.rv)[0] == pool.ADMIN,
               'a roster of the old key A is refused on %s' % b.name)

    # K2 is on again: recover once more sends only to it (no new key A). The state is in the place of an older poolmgr
    # (recover.json at the top of the data folder): recover reads it, then writes pool/recover.json and removes it.
    p = pool.Pool.load(data2.pool_dir)
    p.find('K2')['ip'] = k2_ip
    p.save(data2.pool_dir)
    a2 = pc.p256_pub(data2.key_a)
    os.replace(state, os.path.join(root2, 'recover.json'))
    res, out = call(pm_keys.recover, data2, rblob, PHRASE)
    expect(isinstance(res, list) and 'K2' in out and 'K0' not in out, 'recover again: only K2: %s %s' % (res, out))
    p = pool.Pool.load(data2.pool_dir)
    expect(pc.p256_pub(data2.key_a) == a2 and all(same_pool(p, b) for b in boards), 'recover again: K2 has it')
    st = json.loads(open(state, 'rb').read())
    expect(st == {'format': 1, 'pending': []} and not os.path.exists(os.path.join(root2, 'recover.json')),
           'pool/recover.json: no old wk is kept after the end; the recover.json at the top went')
    code, out = tool('--dir', data2.pool_dir, '--key', data2.key_a, 'push')
    expect(code == 0 and all(same_pool(pool.Pool.load(data2.pool_dir), b) for b in boards),
           'push with the new key A: %s' % out[-300:])
    res, out = call(pm_keys.check, bblob, PHRASE, data2)
    expect(isinstance(res, list) and 'BACKUP IS OLD' in ' '.join(res), 'check: the old backup is old now')

    # the command line: backup and check-backup with the pass phrase in the variable
    os.environ[poolmgr.PHRASE_ENV] = PHRASE
    b3 = os.path.join(d, 'cli.tfnbak')
    code, out = call(poolmgr.main, ['--data', root2, 'backup', b3])[0], ''
    expect(code == 0 and os.path.isfile(b3), 'poolmgr backup')
    code = call(poolmgr.main, ['--data', root2, 'check-backup', b3])[0]
    expect(code == 0, 'poolmgr check-backup')
    code = call(poolmgr.main, ['--data', root2, 'check-backup', wrong])[0]
    expect(code == 1, 'poolmgr check-backup of a wrong key: exit 1')

    # no key value, pool key, wk or pass phrase in the logs and in the output
    secrets = [PHRASE, 'PRIVATE KEY']
    for pp in (old, pool.Pool.load(data2.pool_dir)):
        secrets += [pp.pool_k.hex()] + list(pp.wks.values())
    for pem_file in (data.key_a, data2.key_a, old_a):
        secrets.append(b''.join(open(pem_file, 'rb').read().split(b'\n')[1:-2]).decode()[:40])
    secrets.append(body.decode()[:40])
    logs = ''.join(open(dd.log_file, 'rb').read().decode('utf-8') for dd in (data, data2))
    text = logs + OUT.getvalue()
    leak = [s[:12] for s in secrets if s and s in text]
    expect(not leak, 'no secret in the logs or the output (%s)' % leak)
    expect('key ID' in logs or 'key A' in logs, 'the log has the key IDs')
    for b in boards:
        b.srv.shutdown()


def safety_part(d):
    """The faults of the files: a fault in write_new leaves no partial file; backup replaces only an older backup (a
    .tfnkey name, a folder or another file: InputError, before the question of the command line); a NOTE about a
    temporary folder of restore that stays comes at once, also when the restore fails."""
    part = os.path.join(d, 'partial.tfnkey')
    try:
        pm_keys.write_new(part, 'not bytes')
        expect(False, 'write_new: a str must fail')
    except TypeError:
        expect(not os.path.exists(part), 'write_new: a fault in the write leaves no partial file')
    tmp_part(d)
    data = pm_data.Data(os.path.join(d, 'safe'))
    p = pool.Pool.create('SAFE', pm_keys.make_admin(data))
    p.add_manager()
    p.sign(data.key_a)
    p.save(data.pool_dir)
    data.save_cfg()
    rfile = os.path.join(d, 'SAFE-recovery.tfnkey')
    pm_keys.make_recovery(data, rfile, PHRASE)
    note = os.path.join(d, 'notes.txt')
    with open(note, 'wb') as h:
        h.write(b'my notes')
    keep = {f: open(f, 'rb').read() for f in (rfile, note)}
    for out, what in ((rfile, 'the key file of key R'), (os.path.join(d, 'NEW.TFNKEY'), 'a new .tfnkey name'),
                      (note, 'a file that is not a backup'), (d, 'a folder')):
        res, text = call(pm_keys.backup, data, out, PHRASE)
        expect(raises(res, InputError), 'backup into %s: InputError (%r)' % (what, res))
    expect(all(open(f, 'rb').read() == b for f, b in keep.items()) and
           not os.path.exists(os.path.join(d, 'NEW.TFNKEY')), 'backup: the key file of key R and the other file stay '
           'as they were')
    bfile = os.path.join(d, 'SAFE-backup.tfnbak')
    for i in range(2):
        res, text = call(pm_keys.backup, data, bfile, PHRASE)
        expect(isinstance(res, list) and open(bfile, 'rb').read(7) == pm_keys.BAK, 'backup: %s' % (
            'a new file', 'an older backup is replaced')[i])
    replace_part(d, data, bfile)
    import pm_pool
    asked, real = [], pm_pool.ask_secret
    pm_pool.ask_secret = lambda prompt='': asked.append(prompt) or PHRASE
    phrase = os.environ.pop(poolmgr.PHRASE_ENV, None)
    try:
        code, text = call(poolmgr.main, ['--data', data.root, 'backup', rfile])
    finally:
        pm_pool.ask_secret = real
        if phrase is not None:
            os.environ[poolmgr.PHRASE_ENV] = phrase
    expect(code == 2 and not asked and 'never replaced' in text, 'poolmgr backup <the key file of key R>: exit 2 '
           'before the question (%r %r)' % (text[-200:], asked))
    # a restore that fails, and its temporary folder does not go: the NOTE comes at once (no lines are returned)
    keep_move, keep_rm = pm_data.move_parts, pm_data.remove_tree

    def bad_move(root, tmp, parts):
        raise OSError('a test fault of the disk')
    pm_data.move_parts, pm_data.remove_tree = bad_move, lambda path: False
    os.environ[poolmgr.PHRASE_ENV] = PHRASE
    try:
        code, text = call(poolmgr.main, ['--data', os.path.join(d, 'r_note'), 'restore', bfile])
    finally:
        pm_data.move_parts, pm_data.remove_tree = keep_move, keep_rm
        if phrase is None:
            os.environ.pop(poolmgr.PHRASE_ENV)
    expect(code == 2 and 'NOTE: the temporary folder' in text and 'a test fault of the disk' in text,
           'restore with a fault and a temporary folder that stays: the NOTE at once (%r)' % text[-300:])


def replace_part(d, data, bfile):
    """backup replaces an older backup only when the new pass phrase opens it and it is a backup of the same pool: a
    pool with the same name in another data folder (the same default file name) and another pass phrase stop with
    InputError, and the file stays; --replace replaces it."""
    other = pm_data.Data(os.path.join(d, 'safe_other'))
    p = pool.Pool.create('SAFE', pm_keys.make_admin(other))
    p.add_manager()
    p.sign(other.key_a)
    p.save(other.pool_dir)
    other.save_cfg()
    keep = open(bfile, 'rb').read()
    for dd, phrase, words in ((other, PHRASE, 'is the backup of another pool (SAFE, ID'),
                              (data, PHRASE + ' two', 'the new pass phrase does not open')):
        res, text = call(pm_keys.backup, dd, bfile, phrase)
        expect(raises(res, InputError) and words in str(res) and '--replace' in str(res) and
               open(bfile, 'rb').read() == keep, 'backup over %s: InputError, the file stays (%r)' % (words, res))
    env = os.environ.get(poolmgr.PHRASE_ENV)
    os.environ[poolmgr.PHRASE_ENV] = PHRASE
    try:
        code, text = call(poolmgr.main, ['--data', other.root, 'backup', bfile])
        expect(code == 2 and 'another pool' in text and open(bfile, 'rb').read() == keep,
               'poolmgr backup over the backup of another pool: exit 2 (%s)' % text[-200:])
        code, text = call(poolmgr.main, ['--data', other.root, 'backup', bfile, '--replace'])
    finally:
        if env is None:
            os.environ.pop(poolmgr.PHRASE_ENV, None)
        else:
            os.environ[poolmgr.PHRASE_ENV] = env
    got = pm_keys.pool_of(pm_keys.unzip(pm_keys.open_(open(bfile, 'rb').read(), PHRASE)[1]))
    expect(code == 0 and got.pool_id == p.pool_id, 'poolmgr backup --replace: the backup of the other pool (%s)' %
           text[-200:])
    res, text = call(pm_keys.backup, data, bfile, PHRASE, True)
    got = pm_keys.pool_of(pm_keys.unzip(pm_keys.open_(open(bfile, 'rb').read(), PHRASE)[1]))
    expect(isinstance(res, list) and got.pool_id == data.load().pool_id, 'backup(replace=True): the backup of SAFE '
           'again')
    newer_part(d, data, bfile)


def newer_part(d, data, bfile):
    """An older copy of the data folder (a second computer) does not replace a newer backup of the same pool: a
    higher roster version, or another key A (a recover). InputError names both states, and the file stays;
    --replace replaces it."""
    old = os.path.join(d, 'safe_old_copy')
    shutil.copytree(data.root, old)
    p = data.load()
    p.sign(data.key_a)
    p.save(data.pool_dir)
    res, text = call(pm_keys.backup, data, bfile, PHRASE)
    expect(isinstance(res, list), 'backup of the newer state over the older backup (%r)' % (res,))
    keep = open(bfile, 'rb').read()
    res, text = call(pm_keys.backup, pm_data.Data(old), bfile, PHRASE)
    msg = str(res)
    expect(raises(res, InputError) and 'is a NEWER backup of the pool SAFE' in msg and
           'roster version %d' % p.version in msg and 'roster version %d' % (p.version - 1) in msg and
           '--replace' in msg and open(bfile, 'rb').read() == keep, 'backup of an older copy over a newer backup: '
           'InputError, the file stays (%r)' % (res,))
    env = os.environ.get(poolmgr.PHRASE_ENV)
    os.environ[poolmgr.PHRASE_ENV] = PHRASE
    try:
        code, text = call(poolmgr.main, ['--data', old, 'backup', bfile])
    finally:
        if env is None:
            os.environ.pop(poolmgr.PHRASE_ENV, None)
        else:
            os.environ[poolmgr.PHRASE_ENV] = env
    expect(code == 2 and 'NEWER backup' in text and open(bfile, 'rb').read() == keep,
           'poolmgr backup of an older copy over a newer backup: exit 2 (%s)' % text[-300:])
    # another key A (as after a recover on the other computer), with a roster version that is not lower
    other_a = os.path.join(d, 'safe_other_a')
    shutil.copytree(data.root, other_a)
    da = pm_data.Data(other_a)
    pa = da.load()
    os.remove(da.key_a)
    pa.admins[0] = fw_sign.keygen(da.key_a)
    pa.sign(da.key_a)
    pa.save(da.pool_dir)
    res, text = call(pm_keys.backup, da, bfile, PHRASE)
    expect(raises(res, InputError) and 'is a DIFFERENT backup of the pool SAFE' in str(res) and
           pm_keys.kid(p.admins[0]) in str(res) and open(bfile, 'rb').read() == keep,
           'backup with another key A over a backup: InputError, the file stays (%r)' % (res,))
    res, text = call(pm_keys.backup, pm_data.Data(old), bfile, PHRASE, True)
    got = pm_keys.pool_of(pm_keys.unzip(pm_keys.open_(open(bfile, 'rb').read(), PHRASE)[1]))
    expect(isinstance(res, list) and got.version == p.version - 1, 'backup --replace: the older copy replaces the '
           'newer backup (%r)' % (res,))
    more_part(d, old, bfile)


def write_webpass(data, wp):
    with open(os.path.join(data.pool_dir, 'webpass.json'), 'wb') as h:
        h.write(json.dumps(wp).encode())


def more_part(d, old, bfile):
    """The same roster version and epoch: a backup with a part that the data folder does not have (the pool store key
    of "store key", the web password of a board) is newer than the data folder: InputError, the file stays. A data
    folder with more parts, or with another value of a web password, replaces the backup."""
    a, b = pm_data.Data(old), pm_data.Data(os.path.join(d, 'safe_copy_b'))
    shutil.copytree(old, b.root)
    write_webpass(a, {'AABBCC000001': 'one', 'AABBCC000002': 'two'})
    write_webpass(b, {'AABBCC000001': 'one'})
    fw_sign.keygen(a.key_store)
    res, text = call(pm_keys.backup, a, bfile, PHRASE)
    expect(isinstance(res, list), 'backup of a folder with more parts over the older backup (%r)' % (res,))
    keep = open(bfile, 'rb').read()
    res, text = call(pm_keys.backup, b, bfile, PHRASE)
    msg = str(res)
    expect(raises(res, InputError) and 'parts that the data folder does not have' in msg and
           'keys/store_pool.pem' in msg and 'AABBCC000002' in msg and '--replace' in msg and
           open(bfile, 'rb').read() == keep, 'backup of a copy with no store key over a backup with it: InputError, '
           'the file stays (%r)' % (res,))
    shutil.copyfile(a.key_store, b.key_store)
    res, text = call(pm_keys.backup, b, bfile, PHRASE)
    expect(raises(res, InputError) and 'keys/store_pool.pem' not in str(res) and 'AABBCC000002' in str(res) and
           open(bfile, 'rb').read() == keep, 'backup of a copy with one web password less: InputError (%r)' % (res,))
    write_webpass(b, {'AABBCC000001': 'new one', 'AABBCC000002': 'new two'})
    res, text = call(pm_keys.backup, b, bfile, PHRASE)
    expect(isinstance(res, list), 'backup with other values of the web passwords (webpass new) replaces the backup '
           '(%r)' % (res,))
    newer_copy(d, b, bfile)


def newer_copy(d, b, bfile):
    """A copy of the data folder with a HIGHER roster version (a board adopted on a second computer), but with no pool
    store key or with another one: the key file of the backup stops backup at any version (the program never deletes a
    key file); --replace replaces it."""
    c = pm_data.Data(os.path.join(d, 'safe_copy_c'))
    shutil.copytree(b.root, c.root)
    os.remove(c.key_store)
    p = c.load()
    p.sign(c.key_a)
    p.save(c.pool_dir)
    keep = open(bfile, 'rb').read()
    for why in ('not there', 'other bytes'):
        res, text = call(pm_keys.backup, pm_data.Data(c.root), bfile, PHRASE)
        msg = str(res)
        expect(raises(res, InputError) and '(keys/store_pool.pem) (%s)' % why in msg and
               'roster version %d' % (p.version - 1) in msg and '--replace' in msg and open(bfile, 'rb').read() == keep,
               'backup of a copy with a higher roster version and a store key %s: InputError, the file stays (%r)' % (
                   why, res))
        if not os.path.exists(c.key_store):
            fw_sign.keygen(c.key_store)
    res, text = call(pm_keys.backup, pm_data.Data(c.root), bfile, PHRASE, True)
    expect(isinstance(res, list) and open(bfile, 'rb').read() != keep, 'backup --replace over a backup with another '
           'store key (%r)' % (res,))


def tmp_part(d):
    """write_file of pm_data and of pool (Pool.save, webpass.json): a replace that fails (a virus scanner holds the
    file) and a write that fails remove the temporary file <name>.<pid>.tmp: no copy of a key stays next to the
    file."""
    for who, write in (('pm_data', pm_data.write_file), ('pool', pool.write_file)):
        target = os.path.join(d, 'admin_try_%s.pem' % who)
        tmp = '%s.%d.tmp' % (target, os.getpid())
        real, tries = os.replace, []

        def held(src, dst):
            tries.append(src)
            raise PermissionError(13, 'a test fault: another process holds the file')
        os.replace = held
        try:
            write(target, b'a key in clear')
            expect(False, '%s.write_file: the replace fault must come out' % who)
        except PermissionError:
            expect(len(tries) == 20 and not os.path.exists(tmp) and not os.path.exists(target),
                   '%s.write_file: after 20 tries of the replace, no temporary file stays (%d tries)' %
                   (who, len(tries)))
        finally:
            os.replace = real
        try:
            write(target, 'not bytes')
            expect(False, '%s.write_file: a str must fail' % who)
        except TypeError:
            expect(not os.path.exists(tmp) and not os.path.exists(target), '%s.write_file: a fault in the write '
                   'leaves no temporary file' % who)
    # a replace that Windows refuses two times, then lets go: the file is there
    target = os.path.join(d, 'pool_try.json')
    real, tries = os.replace, []

    def busy(src, dst):
        tries.append(src)
        if len(tries) < 3:
            raise PermissionError(13, 'a test fault: another process holds the file')
        return real(src, dst)
    os.replace = busy
    try:
        pool.write_file(target, b'{}', 0o600)
    finally:
        os.replace = real
    expect(len(tries) == 3 and open(target, 'rb').read() == b'{}', 'pool.write_file: the replace tries again')


class FakeRun:
    """A fake subprocess.run: it keeps (the program, creationflags) of each call and gives an answer with exit 0."""

    def __init__(self):
        self.calls = []

    def __call__(self, argv, **k):
        self.calls.append((os.path.basename(argv[0]).lower(), k.get('creationflags')))
        return subprocess.CompletedProcess(argv, 0, b'"user","S-1-5-21-1-2-3-1001"\r\n', b'')


def nowindow_part(d):
    """Each program that poolmgr starts (whoami, icacls, openssl) gets creationflags = CREATE_NO_WINDOW (0 on Linux
    and macOS): the service of Windows runs pythonw.exe, which has no console, so each program would open a window of
    its own. The test runs the Windows paths with a fake subprocess.run on each system."""
    want = getattr(subprocess, 'CREATE_NO_WINDOW', 0)
    fake = FakeRun()
    real = (subprocess.run, sys.platform, fw_sign.openssl, list(pm_data._SID))
    subprocess.run, sys.platform, fw_sign.openssl = fake, 'win32', lambda: 'openssl'
    try:
        del pm_data._SID[:]
        pm_data.user_sid()
        pm_data.protect(d)
        pm_data.aces(d)
        pm_keys.ssl(['version'])
        fw_sign.run_ssl(['version'])
        fw_sign.verify_raw(bytes([4]) + bytes(64), b'msg', (1).to_bytes(32, 'big') * 2)
    finally:
        subprocess.run, sys.platform, fw_sign.openssl = real[:3]
        pm_data._SID[:] = real[3]
    names = [n for n, _ in fake.calls]
    expect(names == ['whoami.exe', 'icacls.exe', 'icacls.exe', 'openssl', 'openssl', 'openssl'],
           'NO_WINDOW: the programs of the test (%r)' % names)
    expect(all(f == want for _, f in fake.calls), 'NO_WINDOW: each program starts with creationflags %d (%r)' %
           (want, fake.calls))


def check_compare(bblob, data2):
    """check-backup compares each file of the backup with the data folder: the web passwords, the store tokens, the
    store mode of config.json (not the address of the local store), the pool store key."""
    res, out = call(pm_keys.check, bblob, PHRASE, data2)
    expect(isinstance(res, list) and 'the backup has the state of the data folder (each file is the same)' in res,
           'check: the restored folder has the state of the backup: %s' % res)
    names = ('pool/webpass.json', 'config.json')
    keep = {n: open(os.path.join(data2.root, *n.split('/')), 'rb').read() for n in names}
    wp = json.loads(keep['pool/webpass.json'].decode())
    wp[sorted(wp)[0]] = 'another web password'
    cfg = json.loads(keep['config.json'].decode())
    cfg.update(store='local', lan_url='http://192.0.2.7:47480')
    tok = os.path.join(data2.pool_dir, 'store_tok.json')
    for path, value in ((os.path.join(data2.pool_dir, 'webpass.json'), wp), (data2.cfg_file, cfg), (tok, {'AA': 'x'})):
        with open(path, 'wb') as h:
            h.write(json.dumps(value).encode())
    fw_sign.keygen(data2.key_store)
    try:
        res, out = call(pm_keys.check, bblob, PHRASE, data2)
        text = '\n'.join(res) if isinstance(res, list) else str(res)
        for want in ('BACKUP IS OLD: the web passwords of the boards (pool/webpass.json): not the same',
                     'BACKUP IS OLD: the store tokens of the boards (pool/store_tok.json): only in the data folder',
                     'BACKUP IS OLD: config.json: the store mode (the backup: official, the data folder: local)',
                     'BACKUP IS OLD: the pool store key (keys/store_pool.pem): only in the data folder',
                     'BACKUP IS OLD: make a new backup'):
            expect(want in text, 'check: %s (%s)' % (want, text[-600:]))
        expect('lan_url' not in text and res[-1] == 'CHECK OK', 'check: the address of the local store is not '
               'compared; the backup itself is good')
    finally:
        for n in names:
            with open(os.path.join(data2.root, *n.split('/')), 'wb') as h:
                h.write(keep[n])
        os.remove(tok)
        os.remove(data2.key_store)
    res, out = call(pm_keys.check, bblob, PHRASE, data2)
    expect(isinstance(res, list) and 'BACKUP IS OLD' not in ' '.join(res), 'check: the same state again')


def main():
    t0 = time.time()
    with tempfile.TemporaryDirectory(prefix='poolmgr_keys_') as d:
        files_part(d)
        pool_part(d)
        safety_part(d)
        nowindow_part(d)
    if FAILS:
        print('POOLMGR KEYS PY FAILED: %d of %d checks' % (len(FAILS), CHECKS[0]))
        return 1
    print('POOLMGR KEYS PY OK (%d checks, KDF %s, %.1f s)' % (CHECKS[0], 'scrypt + pbkdf2' if pm_keys.has_scrypt()
                                                              else 'pbkdf2 only', time.time() - t0))
    return 0


if __name__ == '__main__':
    sys.exit(main())
