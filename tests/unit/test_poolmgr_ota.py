#!/usr/bin/env python3
"""The test of the firmware updates of poolmgr (poolmgr/pm_ota.py, P6 design 2.10 and 3.1).

No real board, no network download: a fake channel (the files of site/tools/fw_manifest.py and of a GitHub release
of sdk/tools/release_pack.py, signed with TEST keys of this run) in a temporary folder, served by http.server on
127.0.0.1, and fake boards (FakeOtaBoard: the fake board of poolmgr_fake.py + GET /api/boards + POST /api/ota with the
check of the board, fw_sign.check, ENFORCE). The checks:
  - check_file: a good file; a wrong key, a wrong kind, an old sec_ver, a cut file, a changed byte;
  - the channel: the index, a wrong SHA-256, a cut file, a release with SHA256SUMS;
  - update: no send for a file of a wrong key, a wrong kind, an old sec_ver, a cut file, a wrong SHA-256, a key that
    the board does not have; the refusal of the board ("sig"); one board at a time (the next board only after the
    first one runs the new version with "pend" false); a busy board waits (and --now); a rollback stops the run;
    up to date; a pinned version (--version) from the release files;
  - the command line: "update check" and a failed "update --all" (exit 1).

Usage: python tests/unit/test_poolmgr_ota.py
"""
import contextlib
import io
import os
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
PM = os.path.join(ROOT, 'poolmgr')
sys.path.insert(0, HERE)
sys.path.insert(0, PM)
import pm_data  # noqa: E402
import pm_ota  # noqa: E402
import poolmgr  # noqa: E402
from pm_shared import Failed, fw_sign, pool  # noqa: E402
from poolmgr_fake import FakeOtaBoard, image, make_channel, serve_folder  # noqa: E402
from poolmgr_fake import signed_image as signed  # noqa: E402

sys.path.insert(0, os.path.join(ROOT, 'sdk', 'tools'))
import release_pack  # noqa: E402

FAILS = []
CHECKS = [0]
OLD, NEW, PIN = '1.0.1', '1.0.2', '1.0.3'


def expect(cond, what):
    CHECKS[0] += 1
    if not cond:
        FAILS.append(what)
        print('FAIL', what)


# ---------------------------------------------------------------- the fake files
def write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'wb') as h:
        h.write(data)


# ---------------------------------------------------------------- the run
def cli(*argv):
    out = io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
        code = poolmgr.main(list(argv))
    return code, out.getvalue()


def quiet(fn, *a, **k):
    """fn(*a, **k) with no progress lines: (the result or the Failed exception)."""
    with contextlib.redirect_stdout(io.StringIO()):
        try:
            return fn(*a, **k)
        except Failed as e:
            return e


def run(d):
    pm_ota.POLL_S, pm_ota.WAIT_S = 0.1, 6.0
    k1, k2 = os.path.join(d, 'k1.pem'), os.path.join(d, 'k2.pem')
    p1, p2 = fw_sign.keygen(k1), fw_sign.keygen(k2)
    id1, id2 = fw_sign.key_id(p1).hex(), fw_sign.key_id(p2).hex()

    # ---- check_file (the checks of the program)
    data = pm_data.Data(os.path.join(d, 'data'))
    data.make()
    data.cfg['channel'] = {'format': 1, 'pages': 'http://127.0.0.1:9/', 'repo': 'example/tfn-os', 'store_pub': '',
                           'ota_pub': [p1.hex()]}
    good = signed('lcd349', NEW, k1)
    expect(pm_ota.check_file(data, good, 'lcd349', NEW, 0) == ('ok', id1, 1), 'check_file: a good file')
    expect(pm_ota.check_file(data, good, 'lcd349', NEW, 1)[0] == 'ok', 'check_file: sec_ver 1 = minsv 1')
    expect(pm_ota.check_file(data, good, 'lcd349', NEW, 2)[0] == 'sig_old', 'check_file: sec_ver 1 < minsv 2')
    expect(pm_ota.check_file(data, good, 'cores3', NEW, 0)[0] == 'sig_kind', 'check_file: another kind')
    expect(pm_ota.check_file(data, good, 'lcd349', PIN, 0)[0] == 'sig_kind', 'check_file: another version')
    expect(pm_ota.check_file(data, signed('lcd349', NEW, k2), 'lcd349', NEW, 0)[0] == 'sig_key',
           'check_file: a key that is not an official key')
    expect(pm_ota.check_file(data, good[:-100], 'lcd349', NEW, 0)[0] == 'sig_none', 'check_file: a cut file')
    flip = bytearray(good)
    flip[5000] ^= 1
    expect(pm_ota.check_file(data, bytes(flip), 'lcd349', NEW, 0)[0] == 'sig_hash', 'check_file: a changed byte')
    expect(pm_ota.check_file(data, image('lcd349', NEW), 'lcd349', NEW, 0)[0] == 'sig_none',
           'check_file: no trailer')

    # ---- the channels (one folder each) and the release files
    web = os.path.join(d, 'web')
    files = {'lcd349': (NEW, good), 'cores3': (NEW, signed('cores3', NEW, k1))}
    make_channel(os.path.join(web, 'good'), files)
    make_channel(os.path.join(web, 'sha'), files, lambda k, f: f.update(sha256='0' * 64))
    make_channel(os.path.join(web, 'cut'), files)
    write(os.path.join(web, 'cut', 'firmware', 'lcd349', 'app.bin'), good[:-300])
    make_channel(os.path.join(web, 'key'), {'lcd349': (NEW, signed('lcd349', NEW, k2))})
    make_channel(os.path.join(web, 'kind'), {'lcd349': (NEW, signed('cores3', NEW, k1))})
    make_channel(os.path.join(web, 'cutsig'), {'lcd349': (NEW, good[:-300])})
    make_channel(os.path.join(web, 'old'), {'lcd349': (NEW, signed('lcd349', NEW, k1, sec_ver=0))})
    rel = os.path.join(web, 'rel')
    pin_fw = make_channel(os.path.join(d, 'pin'), {'lcd349': (PIN, signed('lcd349', PIN, k1)),
                                                    'cores3': (PIN, signed('cores3', PIN, k1))})
    os.makedirs(os.path.join(d, 'store'))
    write(os.path.join(d, 'store', 'index.json'), b'{}')
    write(os.path.join(d, 'sdk.zip'), b'PK\x05\x06' + b'\0' * 18)
    names = release_pack.pack(PIN, pin_fw, os.path.join(d, 'store'), os.path.join(d, 'sdk.zip'),
                              os.path.join(rel, 'v' + PIN), p1.hex(), p1.hex())
    expect('tfn-os-%s-lcd349.bin' % PIN in names and 'SHA256SUMS' in names, 'release_pack: the release files')
    write(os.path.join(rel, 'v9.9.9', 'tfn-os-9.9.9-lcd349.bin'), good)
    write(os.path.join(rel, 'v9.9.9', 'SHA256SUMS'), ('%s  tfn-os-9.9.9-lcd349.bin\n' % ('1' * 64)).encode())
    srv, base = serve_folder(web)

    def use(ch):
        data.cfg['channel'].update(pages='%s/%s/' % (base, ch), releases='%s/rel' % base)
        data.save_cfg()

    # ---- the pool: 4 fake boards (2 x lcd349, cores3, tab5) and key A
    a_pub = fw_sign.keygen(data.key_a)
    bs = [FakeOtaBoard(n, bytes.fromhex('aabbcc00000%d' % i), d, kind, [p1])
          for i, (n, kind) in enumerate((('ALPHA', 'lcd349'), ('BRAVO', 'lcd349'), ('CHARLIE', 'cores3'),
                                         ('DELTA', 'tab5')), 1)]
    p = pool.Pool.create('OTATEST', a_pub)
    p.add_manager()
    for b in bs:
        p.upsert({'id': b.id.hex(), 'pk': b.pk.hex(), 'mac': b.mac.hex(), 'role': pool.ROLE_MEMBER, 'flags': 0,
                  'name': b.name, 'bx': b.bx_pub.hex(), 'ip': b.ip})
    raw = p.sign(data.key_a)
    for b in bs:
        b.take(pool.roster_parse(raw), raw)
        b.wk, b.pool_k, b.epoch = p.wk(b.id.hex()), p.pool_k, p.epoch
    p.save(data.pool_dir)
    data.load()
    A, B, C, D = bs

    def sends():
        return sum(len(b.sends) for b in bs)

    # ---- no send for a bad file
    for ch, want in (('sha', 'SHA-256'), ('cut', 'cut file'), ('key', 'sig_key'), ('kind', 'sig_kind'),
                     ('cutsig', 'sig_none')):
        use(ch)
        r = quiet(pm_ota.update, data, ['ALPHA'], None, False)
        expect(isinstance(r, Failed) and want in str(r), 'update %s: Failed with %s (%s)' % (ch, want, r))
        expect(sends() == 0, 'update %s: no send' % ch)
    use('old')
    A.minsv = 1
    r = quiet(pm_ota.update, data, [], None, False)
    expect(isinstance(r, Failed) and r.rows[0]['sig'] == 'sig_old', 'update: sec_ver 0 < minsv 1 = sig_old')
    expect(sends() == 0 and r.rows[1]['result'] == pm_ota.R_STOP, 'sig_old: no send, the run stops')
    A.minsv = 0

    # ---- the channel and update check
    use('good')
    idx = pm_ota.channel_index(data)
    expect(sorted(idx['kinds']) == ['cores3', 'lcd349'], 'channel_index: the kinds')
    expect(pm_ota.fetch(data, 'lcd349') == good, 'fetch: the newest file of the channel')
    expect(os.path.isfile(os.path.join(data.cache, 'fw', NEW, 'lcd349.bin')), 'fetch: the cache')
    rows = {r['member']: r for r in pm_ota.plan(data, [], None)}
    expect([rows[n]['state'] for n in ('ALPHA', 'BRAVO', 'CHARLIE')] == ['update'] * 3, 'plan: update %s' % rows)
    expect(rows['DELTA']['state'] == pm_ota.R_NOCH and rows['DELTA']['kind'] == 'tab5', 'plan: no channel for tab5')
    expect(rows['ALPHA']['now'] == OLD and rows['ALPHA']['newest'] == NEW, 'plan: the versions')

    A.shown_keys = [id2]
    r = quiet(pm_ota.update, data, ['ALPHA'], None, False)
    expect(isinstance(r, Failed) and r.rows[0]['sig'] == 'sig_key' and sends() == 0,
           'update: the board does not have the key: no send (%s)' % r)
    A.shown_keys = [id1]
    A.trust = [p2]  # (the board checks again: it says sig_key)
    r = quiet(pm_ota.update, data, [], None, False)
    expect(isinstance(r, Failed) and r.rows[0]['sig'] == 'sig_key' and 'refused by the board' in
           r.rows[0]['result'], 'update: the refusal of the board (%s)' % getattr(r, 'rows', r))
    expect(len(A.sends) == 1 and sends() == 1 and A.ver == OLD, 'the refusal: one send, the next board waits')
    A.shown_keys, A.trust, A.sends, A.ota_code, A.last_sig = None, [p1], [], '', ''

    # ---- a good update: one board at a time, a busy board waits
    B.app = 'CHESS'
    r = quiet(pm_ota.update, data, [], None, False)
    expect(isinstance(r, list), 'update: no failure (%s)' % r)
    rows = {x['member']: x for x in r} if isinstance(r, list) else {}
    expect(rows.get('ALPHA', {}).get('result') == 'ok' and rows['ALPHA'].get('sig') == 'ok' and
           rows['ALPHA'].get('key') == id1 and rows['ALPHA'].get('sec_ver') == 1, 'update: ALPHA ok %s' % rows)
    expect(rows.get('CHARLIE', {}).get('result') == 'ok', 'update: CHARLIE ok')
    expect(rows.get('BRAVO', {}).get('result', '').startswith(pm_ota.R_BUSY) and not B.sends,
           'update: BRAVO is busy: no send')
    expect(rows.get('DELTA', {}).get('result') == pm_ota.R_NOCH and not D.sends, 'update: DELTA no channel')
    expect(A.ver == NEW and C.ver == NEW and A.minsv == 1, 'update: the new version, minsv 1')
    expect(len(A.sends) == 1 and len(C.sends) == 1 and C.sends[0] > A.done_t[0],
           'one board at a time: CHARLIE only after ALPHA marked itself good')
    r = quiet(pm_ota.update, data, ['BRAVO'], None, True)
    expect(isinstance(r, list) and r[0]['result'] == 'ok' and B.ver == NEW, 'update --now: the busy board')
    B.app = 'MENU'
    n = sends()
    r = quiet(pm_ota.update, data, [], None, False)
    expect(isinstance(r, list) and all(x['result'] in (pm_ota.R_SAME, pm_ota.R_NOCH) for x in r) and sends() == n,
           'update again: up to date, no send')

    # ---- a pinned version from the release; a rollback stops the run
    r = quiet(pm_ota.fetch, data, 'lcd349', '9.9.9')
    expect(isinstance(r, Failed) and 'SHA-256' in str(r), 'fetch --version: SHA256SUMS does not agree')
    r = quiet(pm_ota.fetch, data, 'tab5', PIN)
    expect(isinstance(r, Failed) and pm_ota.R_NOCH in str(r), 'fetch --version: no file for tab5')
    A.rollback = True
    r = quiet(pm_ota.update, data, [], PIN, False)
    expect(isinstance(r, Failed) and r.rows[0]['result'].startswith('rollback') and not B.sends[1:] and
           A.ver == NEW and A.bad == 'ota_1', 'update: a rollback stops the run (%s)' % getattr(r, 'rows', r))
    A.rollback = False
    r = quiet(pm_ota.update, data, [], PIN, False)
    ok = isinstance(r, list) and [x['result'] for x in r] == ['ok', 'ok', 'ok', pm_ota.R_NOCH]
    expect(ok and A.ver == B.ver == C.ver == PIN, 'update --version %s: from the release files (%s)' % (PIN, r))

    # ---- the command line
    code, out = cli('--data', data.root, 'update', 'check')
    expect(code == 0 and 'no channel for this kind' in out and 'up to date' in out, 'cli update check: %s' % out)
    use('key')
    code, out = cli('--data', data.root, 'update', '--all')
    expect(code == 0 and 'up to date' in out, 'cli update --all: the boards are newer than the channel (%s)' % out)
    C.ver = OLD
    code, out = cli('--data', data.root, 'update', 'CHARLIE')
    expect(code == 0 and C.ver == OLD and 'no channel' in out, 'cli: the key channel has no cores3 (%s)' % out)
    A.ver = OLD
    code, out = cli('--data', data.root, 'update', '--all')
    expect(code == 1 and 'FAILED' in out and 'sig_key' in out and A.ver == OLD, 'cli update --all: exit 1 (%s)' % out)
    log = open(data.log_file, 'rb').read().decode()
    expect('update ALPHA lcd349' in log and 'sig_key' in log, 'the log of the updates')
    srv.shutdown()
    for b in bs:
        b.srv.shutdown()


def main():
    t0 = time.time()
    with tempfile.TemporaryDirectory(prefix='poolmgr_ota_') as d:
        run(d)
    if FAILS:
        print('POOLMGR OTA PY FAILED: %d of %d checks' % (len(FAILS), CHECKS[0]))
        return 1
    print('POOLMGR OTA PY OK (%d checks, %.1f s)' % (CHECKS[0], time.time() - t0))
    return 0


if __name__ == '__main__':
    sys.exit(main())
