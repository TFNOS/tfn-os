#!/usr/bin/env python3
"""The test of poolmgr/ as one program (P6 design 3.1): the command line, doctor, the data folder, the contract of
the modules (design 2.15), and THE STRANGER FLOW from the start to the end on 3 fake boards (tests/unit/poolmgr_fake.py
PoolBoard). No real board and no network: a temporary folder, the address 127.0.0.1, free ports, TEST keys.

THE STRANGER FLOW (each step through the command line of poolmgr.py, as a person types it):
   1. the release files of a version with TEST keys (sdk/tools/release_pack.py --poolmgr): channel.json,
      tfn-keys.json, tfn-poolmgr-V.zip (unpacked in a new folder, "doctor" runs with no repo near it);
   2. init with the channel.json of the release (no key R in clear on the disk; check-backup of both files);
   3. adopt of 3 boards: the USB console (the program reads the 6 digits), the 6 digits that the person types (the
      board of the 'A' 'J' packet), the token JOIN of an old board (pool_tool.py on the pool folder of poolmgr) while
      board 1 is off (it keeps the old roster);
   4. friends (each pair) and a web password for the board of the token JOIN;
   5. the store: store local, serve (the local store), store sync, store key, store publish of a pool app, store
      trust (the console "pub allow", then a tap); each board installs a mirror app and the pool app;
   6. the OTA: update check, update --all --version V from the release files (one board at a time);
   7. serve: one round of the upkeep gives board 1 the newest roster (the real UDP listener sees the beacons);
   8. remove of a board that does not answer (it keeps the old key); serve gives the listener the previous key too;
   9. backup, check-backup;
  10. THE STOLEN COMPUTER: restore into a new data folder, list --live, recover with key R (the old key A is out),
      push; the old data folder (key A and the old request keys) is refused; a roster of the old key A is refused;
      the removed board is not a member and its key opens no beacon of the pool; the new backup has
      pool/recover.json.

Usage: python tests/unit/test_poolmgr_flow.py
"""
import builtins
import contextlib
import inspect
import io
import json
import os
import socket
import stat
import subprocess
import sys
import tempfile
import time
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
PM = os.path.join(ROOT, 'poolmgr')
sys.path.insert(0, HERE)
sys.path.insert(0, PM)
import pm_board  # noqa: E402
import pm_data  # noqa: E402
import pm_keys  # noqa: E402
import pm_net  # noqa: E402
import pm_ota  # noqa: E402
import pm_pool  # noqa: E402
import pm_serve  # noqa: E402
import pm_service  # noqa: E402
import pm_store  # noqa: E402
import poolmgr  # noqa: E402
from pm_shared import fw_sign, pool, pool_tool  # noqa: E402
from pm_shared import pool_crypto as pc  # noqa: E402
from poolmgr_fake import FakeBoard, FakeConsole, PoolBoard, make_channel, make_pkg, make_store  # noqa: E402
from poolmgr_fake import serve_folder, signed_image  # noqa: E402

sys.path.insert(0, os.path.join(ROOT, 'sdk', 'tools'))
import release_pack  # noqa: E402

FAILS = []
CHECKS = [0]
PHRASE = 'a test pass phrase'
OLD, NEW = '1.0.1', '1.0.2'
TOKEN = 'a-family-token-of-the-test'
CONS = {}           # the console port names of the test -> the fake board
REAL_INPUT = builtins.input

# the contract of design 2.15: the module, the function, the names of its parameters
API = [
    (pm_net, 'listen', ['data', 'secs']),
    (pm_net, 'lan_ip_toward', ['ip']),
    (pm_net, 'data_keys', ['data']),
    (pm_board, 'board', ['data', 'member']),
    (pm_board, 'address', ['data', 'member', 'wait']),
    (pm_board, 'post_stream', ['data', 'member', 'path', 'payload', 'progress', 'timeout']),
    (pm_board, 'http_get', ['url', 'timeout', 'limit']),
    (pm_pool, 'init', ['data', 'name', 'keys_file']),
    (pm_pool, 'adopt', ['data', 'ip', 'all_', 'console', 'digits', 'name', 'role', 'webpass', 'friends',
                        'password']),
    (pm_pool, 'list_members', ['data', 'live']),
    (pm_pool, 'friends', ['data', 'check']),
    (pm_pool, 'push', ['data', 'members']),
    (pm_pool, 'remove', ['data', 'member', 'keep_webpass']),
    (pm_pool, 'channel', ['data', 'path']),
    (pm_pool, 'webpass', ['data', 'op', 'members']),
    (pm_pool, 'upkeep', ['data', 'mac', 'st']),
    (pm_ota, 'channel_index', ['data']),
    (pm_ota, 'fetch', ['data', 'kind', 'version']),
    (pm_ota, 'check_file', ['data', 'payload', 'kind', 'version', 'minsv']),
    (pm_ota, 'plan', ['data', 'members', 'version']),
    (pm_ota, 'update', ['data', 'members', 'version', 'now']),
    (pm_store, 'set_mode', ['data', 'mode']),
    (pm_store, 'sync', ['data']),
    (pm_store, 'route', ['data', 'path', 'query']),
    (pm_store, 'make_key', ['data']),
    (pm_store, 'trust', ['data', 'members', 'console']),
    (pm_store, 'publish', ['data', 'folder']),
    (pm_keys, 'make_admin', ['data']),
    (pm_keys, 'make_recovery', ['data', 'out', 'phrase']),
    (pm_keys, 'seal', ['kind', 'plain', 'phrase']),
    (pm_keys, 'open_', ['blob', 'phrase']),
    (pm_keys, 'backup', ['data', 'out', 'phrase', 'replace']),
    (pm_keys, 'restore', ['blob', 'root', 'phrase']),
    (pm_keys, 'check', ['blob', 'phrase', 'data']),
    (pm_keys, 'recover', ['data', 'recovery_blob', 'phrase', 'new_key']),
    (pm_serve, 'run', ['data', 'port']),
    (pm_service, 'text', ['data', 'program']),
    (pm_service, 'add_args', ['p']),
    (pm_service, 'command', ['a', 'data', 'program']),
]
HELP = ['doctor', 'init', 'channel', 'find', 'adopt', 'list', 'push', 'remove', 'friends', 'webpass', 'update', 'store',
        'backup', 'restore', 'check-backup', 'recover', 'serve', 'service']
MODULES = (pm_pool, pm_net, pm_board, pm_data, pm_ota, pm_store, pm_keys, pm_serve, pm_service)
# "poolmgr.py doctor" of the unpacked zip in a new process with no repo near it (python -I: no PYTHONPATH and no
# folder of the script in sys.path), on the free ports of the test in place of UDP 47474 and TCP 47480
ZIP_DOCTOR = '\n'.join([
    'import os, runpy, sys',
    'pm, data, udp, tcp = sys.argv[1:5]',
    'sys.path.insert(0, pm)',
    'import pm_net, pm_serve',
    'pm_net.UDP_PORT, pm_serve.PORT = int(udp), int(tcp)',
    'sys.argv = [os.path.join(pm, "poolmgr.py"), "--data", data, "doctor"]',
    'runpy.run_path(sys.argv[0], run_name="__main__")'])


def expect(cond, what):
    CHECKS[0] += 1
    if not cond:
        FAILS.append(what)
        print('FAIL', what)


def cli(*argv):
    """poolmgr.main with the output caught: (exit code, the output)."""
    out = io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
        try:
            code = poolmgr.main(list(argv))
        except SystemExit as e:
            code = e.code
    return code, out.getvalue()


def free_port(kind):
    s = socket.socket(socket.AF_INET, kind)
    s.bind(('127.0.0.1', 0))
    p = s.getsockname()[1]
    s.close()
    return p


def free_family_ports():
    """Free ports in place of UDP 47474 (pm_net) and TCP 47480 (pm_serve) in this process: doctor and the listener
    of the test never open the port of a pool manager or of a family manager on this computer."""
    pm_net.UDP_PORT = free_port(socket.SOCK_DGRAM)
    pm_serve.PORT = free_port(socket.SOCK_STREAM)


def same_pool(p, b):
    """The fake board has the newest roster, key and request key of the pool p."""
    return (b.roster is not None and b.roster.pool_id == p.pool_id and b.roster.version == p.version and
            b.pool_k == p.pool_k and b.epoch == p.epoch and b.wk == p.wk(b.id.hex()))


def fmac(b):
    return b.mac.hex().upper()


def wait_for(fn, secs):
    end = time.time() + secs
    while time.time() < end:
        if fn():
            return True
        time.sleep(0.1)
    return fn()


def small_pool(data):
    """A pool with key A and the manager member only, in the data folder of data (no board)."""
    data.make()
    pub = fw_sign.keygen(data.key_a)
    p = pool.Pool.create('SMALL', pub)
    p.add_manager()
    p.sign(data.key_a)
    p.save(data.pool_dir)
    return p


def tree(folder):
    """The names and the sizes in a folder (None when it is not there): the test sees each change."""
    if not os.path.exists(folder):
        return None
    out = []
    for base, dirs, files in os.walk(folder):
        out += [(os.path.relpath(os.path.join(base, n), folder), None) for n in dirs]
        out += [(os.path.relpath(os.path.join(base, n), folder), os.path.getsize(os.path.join(base, n)))
                for n in files]
    return sorted(out)


# ---------------------------------------------------------------- part 1: the program
def step_program(d):
    data_dir = os.path.join(d, 'prog')
    # the help of the program and of each command (a separate process, as a person starts it)
    r = subprocess.run([sys.executable, os.path.join(PM, 'poolmgr.py'), '--help'], capture_output=True,
                       encoding='utf-8', errors='replace')
    expect(r.returncode == 0 and all(c in r.stdout for c in HELP), '--help lists each command')
    for c in HELP:
        code, out = cli(c, '--help')
        expect(code == 0 and 'usage: poolmgr %s' % c in out, '%s --help' % c)
    code, out = cli('service', '--help')
    expect('install' in out and '--dry-run' in out and '--system' in out and 'at each logon of your user' in out,
           'service --help: show|install|remove, at each logon of your user')
    # (the channel comes from channel.json of a release: a tfn-keys.json has no web site and no repo)
    code, out = cli('init', '--help')
    expect(code == 0 and 'channel.json of a GitHub release' in out, 'init --help: --keys takes channel.json')

    # doctor on an empty data folder (no channel: no GET)
    code, out = cli('--data', data_dir, 'doctor')
    expect(code == 0 and 'DOCTOR OK' in out and 'not there yet' in out and not os.path.exists(data_dir),
           'doctor: %s' % out[-400:])
    warn = [ln for ln in out.splitlines() if ln.startswith('WARN') and 'the channel' in ln]
    expect(len(warn) == 1 and '--keys <channel.json' in warn[0] and '<tfn-keys.json>' not in warn[0],
           'doctor: the channel hint names channel.json (%r)' % warn)

    # the contract of the modules; each part is done
    for mod, fn, names in API:
        f = getattr(mod, fn, None)
        expect(f is not None and list(inspect.signature(f).parameters) == names,
               '%s.%s(%s)' % (mod.__name__, fn, ', '.join(names)))
    expect(all(getattr(mod, 'DONE', False) for mod in MODULES), 'each module is done')
    expect(callable(getattr(pm_net, 'Listener', None)) and callable(getattr(pm_net.Listener, 'set_keys', None)),
           'pm_net.Listener with set_keys')
    expect(cli('--data', data_dir, 'update')[0] == 2, 'update with no member: exit 2')

    # the data folder
    data = pm_data.Data(data_dir)
    data.cfg['store'] = 'local'
    data.save_cfg()
    d2 = pm_data.Data(data_dir)
    expect(d2.load() is None and d2.cfg['store'] == 'local' and d2.cfg['port'] == 47480, 'config.json round trip')
    data.set_addr('aabbccddeeff', '127.0.0.1', 'MENU', 'ab' * 8)
    expect(not os.path.exists(data.addrs_file), 'addrs.json: a folder with no pool keeps no address')
    try:
        with data.change():
            expect(False, 'the change lock of a folder with no pool must stop')
    except pm_data.InputError as e:
        expect('no pool' in str(e) and 'poolmgr restore <file>' in str(e) and not os.path.exists(data.lock_file),
               'the change lock of a folder with no pool: no pool, no lock file (%s)' % e)
    small_pool(data)
    data.set_addr('aabbccddeeff', '127.0.0.1', 'MENU', 'ab' * 8)
    expect(data.addrs().get('AABBCCDDEEFF', {}).get('ip') == '127.0.0.1', 'addrs.json')
    data.log('a test line')
    with open(data.log_file, 'rb') as h:
        expect(h.read().endswith(b'a test line\n'), 'the log')
    with data.change():
        with data.change():
            expect(data.lock_n == 2, 'the change lock: the same process again')
        t0 = time.time()
        code = subprocess.run([sys.executable, '-c', 'import sys; sys.path.insert(0, %r); import pm_data; '
                               'd = pm_data.Data(%r)\ntry:\n    with d.change(wait=0.3): sys.exit(0)\n'
                               'except pm_data.InputError: sys.exit(5)' % (PM, data_dir)]).returncode
        expect(code == 5 and time.time() - t0 < 20, 'the change lock stops a second process')
    code = subprocess.run([sys.executable, '-c', 'import sys; sys.path.insert(0, %r); import pm_data; '
                           'd = pm_data.Data(%r)\nwith d.change(wait=0.3): sys.exit(0)' % (PM, data_dir)]).returncode
    expect(code == 0, 'the change lock is free after the change')
    expect(pm_net.lan_ip_toward('127.0.0.1') == '127.0.0.1', 'lan_ip_toward')
    code, out = cli('--data', data_dir, 'service', 'install', '--dry-run')
    expect(code == 0 and 'DRY RUN' in out and ' serve' in out, 'service install --dry-run: %s' % out[-300:])
    expect(not os.path.isdir(os.path.join(data_dir, 'service')), 'service --dry-run writes nothing')

    # the fake board answers the HTTP client of poolmgr
    b = FakeBoard('T1', bytes.fromhex('aabbcc000001'), d)
    st = json.loads(pm_board.http_get('http://%s/api/pool' % b.ip).decode())
    expect(st.get('state') == 'none' and st.get('id') == b.id.hex(), 'the fake board: GET /api/pool')
    try:
        pm_board.http_get('http://%s/api/nothing' % b.ip)
        expect(False, 'http_get: a 404 is a fault')
    except poolmgr.Failed as e:
        expect('404' in str(e), 'http_get: 404')
    b.srv.shutdown()


# ---------------------------------------------------------------- part 2: the stranger flow
class Flow:
    def __init__(self, d):
        self.d = d
        self.root = os.path.join(d, 'data')
        self.web = os.path.join(d, 'web')
        self.boards = []
        self.srv = None
        self.serve = None
        self.lines = []
        self.done = []

    def step(self, name):
        self.done.append(name)

    def data(self, root=None):
        dd = pm_data.Data(root or self.root)
        dd.load()
        return dd

    def pool(self, root=None):
        return pm_data.Data(root or self.root).load()

    # ---- 1. the release files with TEST keys
    def release(self):
        d = self.d
        kd = os.path.join(d, 'testkeys')
        os.makedirs(kd)
        self.store_pem, self.ota_pem = os.path.join(kd, 'store.pem'), os.path.join(kd, 'ota.pem')
        self.store_pt, self.ota_pt = fw_sign.keygen(self.store_pem), fw_sign.keygen(self.ota_pem)
        src = os.path.join(d, 'official')
        make_pkg(src, 'alpha', 'lcd349')
        make_pkg(src, 'alpha', 'cores3')
        make_store(src, os.path.join(self.web, 'store'), self.store_pem)
        fw = make_channel(self.web, {k: (NEW, signed_image(k, NEW, self.ota_pem)) for k in ('lcd349', 'cores3')})
        sdk = os.path.join(d, 'sdk.zip')
        with open(sdk, 'wb') as h:
            h.write(b'PK\x05\x06' + b'\0' * 18)
        self.rel = os.path.join(self.web, 'rel', 'v' + NEW)
        # the public values in hex: store_index.py reads a PEM file only with the package "cryptography"
        names = release_pack.pack(NEW, fw, os.path.join(self.web, 'store'), sdk, self.rel, self.store_pt.hex(),
                                  self.ota_pt.hex(), '', PM, 'https://example.invalid/tfn-os', 'test/tfn-os')
        pz = 'tfn-poolmgr-%s.zip' % NEW
        expect(all(n in names for n in ('channel.json', pz, 'tfn-keys.json', 'SHA256SUMS',
                                        'tfn-os-%s-lcd349.bin' % NEW)), 'release_pack --poolmgr: %r' % names)
        with open(os.path.join(self.rel, 'channel.json'), 'rb') as h:
            ch = json.loads(h.read().decode())
        expect(ch['store_pub'] == self.store_pt.hex() and ch['ota_pub'] == [self.ota_pt.hex()] and
               ch['kinds'] == ['cores3', 'lcd349'], 'channel.json: the TEST keys and the kinds')
        # the zip unpacked in a new folder: doctor runs with no repo near it
        unz = os.path.join(d, 'unzipped')
        with zipfile.ZipFile(os.path.join(self.rel, pz)) as z:
            z.extractall(unz)
        expect(os.path.isfile(os.path.join(unz, 'poolmgr', 'pool.py')) and
               os.path.isfile(os.path.join(unz, 'poolmgr', 'channel.json')), 'the zip: the shared modules and '
               'channel.json in poolmgr/')
        r = subprocess.run([sys.executable, '-I', '-c', ZIP_DOCTOR, os.path.join(unz, 'poolmgr'),
                            os.path.join(d, 'zipdata'), str(pm_net.UDP_PORT), str(pm_serve.PORT)], cwd=unz,
                           capture_output=True, encoding='utf-8', errors='replace', timeout=120)
        expect(r.returncode == 0 and 'DOCTOR OK' in r.stdout and os.path.join(unz, 'poolmgr') in r.stdout,
               'the unpacked zip: doctor (%s)' % (r.stdout + r.stderr)[-500:])
        self.srv, self.base = serve_folder(self.web)
        self.step('release files (TEST keys, release_pack --poolmgr, the zip runs doctor)')

    # ---- 2. init
    def init(self):
        code, out = cli('--data', self.root, 'init', 'FLOW', '--keys', os.path.join(self.rel, 'channel.json'))
        expect(code == 0 and 'poolmgr adopt --all' in out, 'init: %s' % out[-600:])
        expect(fw_sign.key_id(self.ota_pt).hex() in out and fw_sign.key_id(self.store_pt).hex() in out,
               'init prints the key IDs of the channel')
        data = self.data()
        p = data.pool
        ch = data.cfg['channel']
        expect(ch['store_pub'] == self.store_pt.hex() and ch['ota_pub'] == [self.ota_pt.hex()] and
               ch['repo'] == 'test/tfn-os', 'init: the channel of the release in config.json')
        expect(p is not None and len(p.admins) == 2 and p.admins[0] == pc.p256_pub(data.key_a), 'init: keys A and R')
        clear = []
        for dp, _, fs in os.walk(self.d):
            for f in fs:
                fp = os.path.join(dp, f)
                with open(fp, 'rb') as h:
                    if b'PRIVATE KEY' not in h.read():
                        continue
                try:
                    if pc.p256_pub(fp) == p.admins[1]:
                        clear.append(fp)
                except Exception:  # (an encrypted key: no clear key)
                    pass
        expect(not clear, 'init: no key R in clear on the disk %s' % clear)
        for f in ('FLOW-recovery.tfnkey', 'FLOW-backup.tfnbak'):
            code, out = cli('--data', self.root, 'check-backup', f)
            expect(code == 0 and 'CHECK OK' in out, 'init: check-backup %s: %s' % (f, out[-300:]))
        # the channel of this test: the web site and the release files on 127.0.0.1 (a real channel.json has the
        # https:// site of the release)
        data.cfg['channel'].update(pages=self.base, releases=self.base + '/rel')
        data.save_cfg()
        self.step('init (no key R in clear, check-backup of both files)')

    # ---- 3. adopt of 3 boards
    def adopt(self):
        d = self.d
        trust = [self.ota_pt]
        b1 = PoolBoard('B1', bytes.fromhex('aabbcc000001'), d, 'lcd349', trust, self.store_pt)
        b2 = PoolBoard('B2', bytes.fromhex('aabbcc000002'), d, 'lcd349', trust, self.store_pt)
        b3 = PoolBoard('B3', bytes.fromhex('aabbcc000003'), d, 'cores3', trust, self.store_pt, legacy=True,
                       token=TOKEN)
        self.boards = [b1, b2, b3]
        CONS.update(CON1=b1, CON2=b2, CON3=b3)
        # (1) the USB console: the program opens the JOIN screen, reads the 6 digits, sends "pool allow"
        code, out = cli('--data', self.root, 'adopt', b1.ip, '--console', 'CON1', '--name', 'B1')
        p = self.pool()
        expect(code == 0 and same_pool(p, b1) and b1.webpw, 'adopt --console: %s' % out[-600:])
        # (2) the digits that the person types: the board with an open JOIN screen ('A' 'J'), a tap on ALLOW
        b2.join_open, b2.auto_tap = True, 0.3
        # (a packet gives the host only: a real board serves on port 80; the fake board has its own port)
        self.data().set_addr(fmac(b2), b2.ip, '', b2.id.hex())
        asked = []

        def typed(prompt=''):
            asked.append(prompt)
            with b2.lock:
                return pool.sas_text(b2.shown_sas())
        builtins.input = typed
        try:
            code, out = cli('--data', self.root, 'adopt')
        finally:
            builtins.input = REAL_INPUT
        p = self.pool()
        m2 = p.find(b2.id.hex()) if p else None
        expect(code == 0 and asked and same_pool(p, b2) and same_pool(p, b1), 'adopt with typed digits: %s' %
               out[-600:])
        expect(m2 is not None and m2['name'] == 'B2', 'adopt: the name of the A J packet')
        # (3) the token JOIN of an old board with pool_tool.py on the pool folder of poolmgr; board 1 is off
        b1.off = True
        data = self.data()
        po = io.StringIO()
        with contextlib.redirect_stdout(po), contextlib.redirect_stderr(po):
            code = pool_tool.main(['--dir', data.pool_dir, '--key', data.key_a, 'adopt', b3.ip, '--token', TOKEN,
                                   '--name', 'B3'])
        b1.off = False
        p = self.pool()
        # (board 1 is off, so it does not take the new roster: pool_tool.py ends with exit 1)
        expect(code == 1 and 'NOT ALL members' in po.getvalue() and same_pool(p, b3) and same_pool(p, b2),
               'the token JOIN (pool_tool.py; board 1 off: exit 1): %s' % po.getvalue()[-400:])
        expect(b1.rv == p.version - 1, 'board 1 was off: it has the old roster v%d (the pool v%d)' % (b1.rv, p.version))
        self.step('adopt of 3 boards (console digits, typed digits + A J, the token JOIN; board 1 off)')

    # ---- 4. the friends
    def friends(self):
        b1, b2, b3 = self.boards
        code, out = cli('--data', self.root, 'friends')
        expect(code == 0 and 'FRIENDS OK' in out, 'friends: %s' % out[-400:])
        for b in self.boards:
            others = {fmac(o) for o in self.boards if o is not b}
            expect({f[0] for f in b.friends} == others, '%s: a friend of each other member (%r)' % (b.name, b.friends))
        code, out = cli('--data', self.root, 'friends', '--check')
        expect(code == 0 and 'FRIENDS OK: 0 new friend entries' in out and 'friends: 0 new friend entries, 0 were '
               'there' in out, 'friends --check: nothing is missing: %s' % out[-300:])
        code, out = cli('--data', self.root, 'webpass', 'new', 'B3')
        code2, out2 = cli('--data', self.root, 'webpass', 'show', 'B3')
        expect(code == 0 and b3.webpw and b3.webpw in out2, 'webpass new for the board of the token JOIN')
        self.step('friends (each pair), webpass of the token board')

    # ---- 5. the store
    def store(self):
        b1, b2, b3 = self.boards
        port = free_port(socket.SOCK_STREAM)
        data = self.data()
        data.cfg['port'] = port
        data.save_cfg()
        code, out = cli('--data', self.root, 'store', 'local')
        lan = 'http://127.0.0.1:%d' % port
        expect(code == 0 and all(b.lan == lan and b.tok for b in self.boards), 'store local: %s' % out[-400:])
        self.serve = pm_serve.Serve(pm_data.Data(self.root), port=port, host='127.0.0.1', udp_port=pm_net.UDP_PORT,
                                    out=self.lines.append)
        self.serve.start()
        expect(self.serve.bound == port and isinstance(self.serve.listener, pm_net.Listener),
               'serve: the local store on its port and the UDP listener')
        code, out = cli('--data', self.root, 'store', 'sync')
        expect(code == 0 and '2 packages' in out, 'store sync: %s' % out[-300:])
        code, out = cli('--data', self.root, 'store', 'key')
        expect(code == 0 and 'the pool store key' in out, 'store key')
        own = os.path.join(self.d, 'own')
        make_pkg(own, 'poolapp', 'lcd349', ver='0.1.0')
        make_pkg(own, 'poolapp', 'cores3', ver='0.1.0')
        code, out = cli('--data', self.root, 'store', 'publish', own)
        expect(code == 0 and 'poolapp' in out, 'store publish: %s' % out[-300:])
        expect(b1.install('poolapp') == (False, 'NOT SIGNED'), 'a pool app before the trust: NOT SIGNED')
        code, out = cli('--data', self.root, 'store', 'trust', 'B1', '--console', 'CON1')
        expect(code == 0 and b1.pubs, 'store trust --console (pub allow): %s' % out[-300:])
        b2.pub_auto = b3.pub_auto = 0.3
        code, out = cli('--data', self.root, 'store', 'trust')
        expect(code == 0 and 'trusted already' in out and b2.pubs and b3.pubs, 'store trust with a tap: %s' %
               out[-400:])
        for b in self.boards:
            expect(b.install('alpha') == (True, '1.0.0'), '%s installs the mirror app' % b.name)
            expect(b.install('poolapp') == (True, '0.1.0'), '%s installs the pool app' % b.name)
        self.step('store local + serve + sync + key + publish + trust (console, tap) + installs')

    # ---- 6. the OTA
    def ota(self):
        code, out = cli('--data', self.root, 'update', 'check')
        expect(code == 0 and out.count('newest=%s' % NEW) == 3, 'update check: %s' % out[-400:])
        code, out = cli('--data', self.root, 'update', '--all', '--version', NEW)
        expect(code == 0 and all(b.ver == NEW and len(b.sends) == 1 for b in self.boards),
               'update --all --version %s: %s' % (NEW, out[-600:]))
        bs = self.boards
        expect(all(bs[i + 1].sends[0] > bs[i].done_t[0] for i in range(len(bs) - 1)),
               'one board at a time: the next send only after the board before marked itself good')
        self.step('OTA from the release files (TEST keys), one board at a time')

    # ---- 7. serve: one round of the upkeep
    def serve_round(self):
        b1 = self.boards[0]
        s = self.serve
        p = self.pool()

        def round_():
            s.tick(time.monotonic())
            return same_pool(p, b1)
        expect(wait_for(round_, 15), 'serve: the upkeep gives board 1 the newest roster (%r %r)' % (
            self.lines[-5:], dict(s.counts)))
        expect(any('UPKEEP B1: old roster' in ln for ln in self.lines), 'serve: the line of the upkeep')
        expect(wait_for(lambda: (s.drain(time.monotonic()) or s.counts['beacons'] > 0), 5),
               'serve: the real listener opens the beacons of the members')
        self.step('serve: one round, the upkeep of the board with the old roster')

    # ---- 8. remove a board that does not answer
    def remove(self):
        b1, b2, b3 = self.boards
        e0 = self.pool().epoch
        b2.off = True
        code, out = cli('--data', self.root, 'remove', 'B2')
        p = self.pool()
        expect(code == 0 and p.find(b2.id.hex()) is None and 'did not answer' in out, 'remove: %s' % out[-600:])
        expect(p.epoch == e0 + 1 and same_pool(p, b1) and same_pool(p, b3), 'remove: a new key for the others')
        expect(b2.epoch == e0 and b2.roster is not None, 'remove: the board that did not answer keeps the old key')
        expect(all(fmac(b2) not in {f[0] for f in b.friends} for b in (b1, b3)), 'remove: the pool friends out')
        s = self.serve
        s.tick(time.monotonic())
        expect([k.epoch for k in s.listener.keys] == [e0 + 1, e0],
               'serve: the listener has the new key and the previous key (its hour)')
        self.step('remove of a board that does not answer; the listener gets the new keys')

    # ---- 9. backup
    def backup(self):
        code, out = cli('--data', self.root, 'backup', 'flow.tfnbak')
        expect(code == 0, 'backup: %s' % out[-300:])
        code, out = cli('--data', self.root, 'check-backup', 'flow.tfnbak')
        expect(code == 0 and 'CHECK OK' in out and 'the backup has the state of the data folder' in out,
               'check-backup: %s' % out[-400:])
        self.serve.stop()
        self.step('backup + check-backup')

    # ---- 10. the stolen computer: restore, recover with key R, the old key A is refused
    def recover(self):
        b1, b2, b3 = self.boards
        root2 = os.path.join(self.d, 'data2')
        old = self.pool()
        code, out = cli('--data', root2, 'restore', 'flow.tfnbak')
        expect(code == 0 and 'restored' in out, 'restore into a new data folder: %s' % out[-300:])
        code, out = cli('--data', root2, 'list', '--live')
        expect(code == 0 and out.count('[ok v%d e%d]' % (old.version, old.epoch)) == 2, 'list --live after the '
               'restore: %s' % out[-500:])
        code, out = cli('--data', root2, 'recover', '--recovery', 'FLOW-recovery.tfnkey')
        p2 = self.pool(root2)
        expect(code == 0 and 'recover done' in out, 'recover: %s' % out[-600:])
        expect(p2.admins[0] != old.admins[0] and p2.admins[1] == old.admins[1] and p2.epoch == old.epoch + 1,
               'recover: a new key A, the same key R, a new pool key')
        expect(same_pool(p2, b1) and same_pool(p2, b3), 'recover: the boards have the new roster and keys')
        code, out = cli('--data', root2, 'push')
        expect(code == 0 and 'PUSH OK: 2' in out, 'push from the new data folder: %s' % out[-300:])
        # the thief: the old data folder (the old key A, the old request keys)
        code, out = cli('--data', self.root, 'push')
        expect(code == 1 and out.count('FAILED: refused: auth') == 2 and same_pool(p2, b1),
               'the old key A and wk are refused (401): %s' % out[-400:])
        data1 = self.data()
        raw = old.sign(data1.key_a)
        res, _ = pool.roster_accept(b1.roster, raw, b1.id, b1.rv)
        expect(res != pool.TAKE, 'a roster of the old key A is refused (%s)' % res)
        # the removed board
        b2.off = False
        code, out = cli('--data', root2, 'push', 'B2')
        expect(code == 2, 'the removed board is not a member: %s' % out[-200:])
        with b2.lock:
            pkt = pool.beacon_seal(b2.keys(), pool.beacon_plain(b2.mac, 'B2', '', 0, b2.id))
        expect(pm_net.parse(pkt, '127.0.0.1', pm_net.data_keys(pm_data.Data(root2))) == (None, None),
               'the key of the removed board opens no beacon of the pool')
        # the new backup has pool/recover.json
        code, out = cli('--data', root2, 'backup', 'flow2.tfnbak')
        with open('flow2.tfnbak', 'rb') as h:
            names = pm_keys.unzip(pm_keys.open_(h.read(), PHRASE)[1])
        expect(code == 0 and 'pool/recover.json' in names and 'recover.json' not in names and
               'keys/admin_a.pem' in names, 'the backup after the recover has pool/recover.json: %r' % sorted(names))
        self.step('the stolen computer: restore + recover with key R; the old key A and the removed board are '
                  'refused')

    def recover_new_key(self):
        """A board is off at a recover; then the computer with the key A of that recover is stolen too: recover with
        no option keeps key A (WARN), recover --new-key makes a new key A, and the board that waits gets it later
        (its old manager ID and old request key stay in pool/recover.json)."""
        b1, b2, b3 = self.boards
        root2 = os.path.join(self.d, 'data2')
        state = os.path.join(root2, 'pool', 'recover.json')
        rec = 'FLOW-recovery.tfnkey'
        b3.off = True
        try:
            code, out = cli('--data', root2, 'recover', '--recovery', rec)
            pa = self.pool(root2)
            expect(code == 1 and 'B3: NOT REACHED' in out and same_pool(pa, b1) and not same_pool(pa, b3),
                   'recover with B3 off: B1 gets the new key A, B3 waits (%s)' % out[-400:])
            code, out = cli('--data', root2, 'recover', '--recovery', rec)
            pb = self.pool(root2)
            expect(code == 1 and 'WARN: key A did NOT change' in out and '--new-key' in out and
                   pb.admins == pa.admins and pb.epoch == pa.epoch, 'recover again with B3 off: the same key A and '
                   'a WARN line (%s)' % out[-400:])
            code, out = cli('--data', root2, 'recover', '--recovery', rec, '--new-key')
            pc_ = self.pool(root2)
            with open(state, 'rb') as h:
                st = json.loads(h.read().decode('utf-8'))
            expect(code == 1 and pc_.admins[0] != pa.admins[0] and pc_.admins[1] == pa.admins[1] and
                   pc_.epoch == pa.epoch + 1 and same_pool(pc_, b1) and st.get('pending') == [b3.id.hex()] and
                   st.get('old_mid_of', {}).get(b3.id.hex()) not in (None, st['old_mid']),
                   'recover --new-key with B3 off: a new key A for B1, B3 waits with its own old manager ID (%s %r)' %
                   (out[-400:], st))
        finally:
            b3.off = False
        code, out = cli('--data', root2, 'recover', '--recovery', rec)
        pd = self.pool(root2)
        with open(state, 'rb') as h:
            st = json.loads(h.read().decode('utf-8'))
        expect(code == 0 and 'recover done' in out and pd.admins == pc_.admins and same_pool(pd, b1) and
               same_pool(pd, b3) and st.get('pending') == [], 'recover when B3 is on: B3 gets the newest key A '
               '(%s)' % out[-400:])
        code, out = cli('--data', root2, 'push')
        expect(code == 0 and 'PUSH OK: 2' in out, 'push after recover --new-key: %s' % out[-300:])
        self.step('recover --new-key while a board waits')

    def close(self):
        if self.serve is not None:
            self.serve.stop()
        for b in self.boards:
            try:
                b.close()
            except OSError:
                pass
        if self.srv is not None:
            self.srv.shutdown()


# ---------------------------------------------------------------- part 3: the data folder
NO_POOL_CMDS = [['push'], ['remove', 'X'], ['backup', 'x.tfnbak'], ['recover', '--recovery', '<key>'],
                ['store', 'key'], ['store', 'local'], ['store', 'official'], ['store', 'sync'], ['store', 'trust'],
                ['store', 'publish', '<pkg>'], ['webpass', 'new'], ['webpass', 'show', 'X'], ['friends'],
                ['friends', '--check'], ['list'], ['list', '--live'], ['adopt', '127.0.0.1:9'], ['update', '--all'],
                ['update', 'check'], ['channel'], ['serve', '--port', '0'], ['service', 'install', '--dry-run'],
                ['find', '--secs', '0.2'], ['doctor'], ['check-backup', '<bak>']]


def step_folder(d):
    """The data folder: each command on a folder with no pool leaves it as it was ("service install" with no pool
    stops); restore takes the empty parts of an older poolmgr and its lock, but not another file; the pass phrase
    questions name their files; the rights of the folder (init sets them; doctor shows other accounts and the
    command that repairs them, and no command with no SID of the user; a user name with a space and a domain); the key
    files of init, restore and recover into a folder that exists with other rights get only the rights of this user;
    the .tfnkey and .tfnbak files in a current folder with other rights get only the rights of this user; store key
    and recover in a data folder of an older poolmgr (keys/ and pool/ with the rights of the folder) give them their
    own rights; a damaged config.json, pool.json, secrets.json, webpass.json, store_tok.json or friends_out.json stops
    with exit 2 (and the last three stay as they are); doctor checks key A (and with no openssl, a WARN)."""
    os.environ.pop(poolmgr.PHRASE_ENV, None)
    kd = os.path.join(d, 'fkeys')
    os.makedirs(kd)
    pubs = [fw_sign.keygen(os.path.join(kd, '%s.pem' % n)) for n in ('store', 'ota')]
    keys = os.path.join(kd, 'channel.json')
    with open(keys, 'w') as h:
        json.dump({'format': 1, 'pages': '', 'repo': '', 'store_pub': pubs[0].hex(), 'ota_pub': [pubs[1].hex()]}, h)
    key_file, bak_file = os.path.join(kd, 'dummy.tfnkey'), os.path.join(kd, 'dummy.tfnbak')
    for path, magic in ((key_file, b'TFNKEY1'), (bak_file, b'TFNBAK1')):
        with open(path, 'wb') as h:
            h.write(magic + bytes(90))
    pkg = os.path.join(kd, 'pkg')
    os.makedirs(pkg)
    gone, empty = os.path.join(d, 'nofolder'), os.path.join(d, 'emptyfolder')
    os.makedirs(empty)
    os.environ[poolmgr.PHRASE_ENV] = PHRASE
    for folder in (gone, empty):
        for c in NO_POOL_CMDS:
            argv = [{'<key>': key_file, '<bak>': bak_file, '<pkg>': pkg}.get(a, a) for a in c]
            before = tree(folder)
            code, out = cli('--data', folder, *argv)
            expect(tree(folder) == before, 'no pool: "%s" leaves the folder as it was (%r)' % (
                ' '.join(c), tree(folder)))
            if c[0] not in ('service', 'find', 'doctor', 'check-backup'):
                expect(code == 2 and 'no pool in' in out and 'poolmgr restore <file>' in out,
                       'no pool: "%s": the STOP line names init and restore (%r)' % (' '.join(c), out[-200:]))
    try:
        pm_service.install(pm_data.Data(gone), os.path.join(PM, 'poolmgr.py'), system='windows',
                           home=os.path.join(d, 'nohome'), run=lambda s: None, out=lambda s: None)
        expect(False, 'service install with no pool must stop')
    except poolmgr.InputError:
        expect(tree(gone) is None and not os.path.exists(os.path.join(d, 'nohome')), 'service install with no pool: '
               'it stops, nothing is written')

    # a pool (init) and its rights; doctor checks key A
    root = os.path.join(d, 'folder')
    code, out = cli('--data', root, 'init', 'FOLDER', '--keys', keys)
    expect(code == 0 and pm_data.rights(root) == ([], ''), 'init: the rights of the new data folder (%s %r)' % (
        out[-200:], pm_data.rights(root)))
    folder_doctor(d, root)
    folder_restore(d, root)
    folder_rights(d)
    folder_key_rights(d, keys)
    folder_out_rights(d, keys)
    folder_old_parts(d)
    folder_damaged(d)
    folder_questions(d, root)


def folder_doctor(d, root):
    data = pm_data.Data(root)
    code, out = cli('--data', root, 'doctor')
    expect('OK   key A: key ID' in out, 'doctor: key A (%s)' % out[-300:])
    away = data.key_a + '.away'
    os.replace(data.key_a, away)
    try:
        code, out = cli('--data', root, 'doctor')
        expect(code == 1 and 'FAIL key A' in out and 'is not there' in out and 'poolmgr recover' in out,
               'doctor: no key A = FAIL, the hint names recover (%s)' % out[-300:])
        fw_sign.keygen(data.key_a)
        code, out = cli('--data', root, 'doctor')
        expect(code == 1 and 'is not the admin key A of the pool' in out and 'poolmgr recover' in out,
               'doctor: a wrong key A = FAIL (%s)' % out[-300:])
        os.replace(data.key_a, data.key_a + '.wrong')
    finally:
        os.replace(away, data.key_a)
    # no openssl: the line of key A is a WARN (not checked) with the openssl hint, not the hint of recover
    keep = os.environ.get('OPENSSL')
    os.environ['OPENSSL'] = os.path.join(d, 'no-openssl-here.exe')
    try:
        code, out = cli('--data', root, 'doctor')
    finally:
        if keep is None:
            os.environ.pop('OPENSSL')
        else:
            os.environ['OPENSSL'] = keep
    key_a = [ln for ln in out.splitlines() if 'key A' in ln]
    expect(code == 1 and 'FAIL openssl' in out and len(key_a) == 1 and key_a[0].startswith('WARN key A: not checked')
           and 'OPENSSL=<path>' in key_a[0] and 'recover' not in key_a[0], 'doctor with no openssl: %r' % key_a)


def folder_key_rights(d, keys):
    """init, restore and recover into a folder that exists and gives rights to other accounts (Windows: the group
    Users; Linux and macOS: the mode 0755): keys/, key A, pool/, the request keys and the state of recover get only the
    rights of this user (and SYSTEM, the administrators); the WARN lines name the rights of the folder."""
    def opened(path):
        os.makedirs(path)
        if sys.platform == 'win32':
            r = subprocess.run([pm_data.win_tool('icacls.exe'), path, '/grant', '*S-1-5-32-545:(OI)(CI)RX'],
                               capture_output=True)
            expect(r.returncode == 0, 'icacls: a test right for the group Users on %s' % path)
        else:
            os.chmod(path, 0o755)

    def private(root, what, more=()):
        for rel in ('keys', os.path.join('keys', 'admin_a.pem'), 'pool', os.path.join('pool', 'secrets.json')) + more:
            p = os.path.join(root, rel)
            if sys.platform == 'win32':
                others = pm_data.rights(p)[0]
                expect(others == [], '%s: other accounts have rights on %s: %r' % (what, rel, others))
            else:
                want = 0o700 if os.path.isdir(p) else 0o600
                expect(stat.S_IMODE(os.stat(p).st_mode) == want, '%s: the mode of %s' % (what, rel))
    r1 = os.path.join(d, 'open_init')
    opened(r1)
    code, out = cli('--data', r1, 'init', 'OPENDIR', '--keys', keys)
    expect(code == 0 and 'WARN' in out, 'init into a folder that exists with other rights: %s' % out[-300:])
    private(r1, 'init into a folder that exists')
    r2 = os.path.join(d, 'open_restore')
    opened(r2)
    code, out = cli('--data', r2, 'restore', os.path.join(d, 'OPENDIR-backup.tfnbak'))
    expect(code == 0 and 'WARN' in out, 'restore into a folder that exists with other rights: %s' % out[-300:])
    private(r2, 'restore into a folder that exists')
    # recover there: the state (old request keys) goes into pool/, not to the top of the folder
    code, out = cli('--data', r2, 'recover', '--recovery', os.path.join(d, 'OPENDIR-recovery.tfnkey'))
    expect(code == 0 and os.path.isfile(os.path.join(r2, 'pool', 'recover.json')) and
           not os.path.exists(os.path.join(r2, 'recover.json')), 'recover in a folder that exists with other rights: '
           'pool/recover.json (%s)' % out[-300:])
    private(r2, 'recover in a folder that exists', (os.path.join('pool', 'recover.json'),))


def folder_out_rights(d, keys):
    """init and backup in a current folder that gives rights to other accounts (Windows: the group Users; Linux and
    macOS: the mode of the files is 0600): the .tfnkey file of key R and the .tfnbak backup get only the rights of this
    user, SYSTEM and the administrators."""
    cwd = os.path.join(d, 'open_cwd')
    os.makedirs(cwd)
    if sys.platform == 'win32':
        r = subprocess.run([pm_data.win_tool('icacls.exe'), cwd, '/grant', '*S-1-5-32-545:(OI)(CI)M'],
                           capture_output=True)
        expect(r.returncode == 0, 'icacls: a test right for the group Users on %s' % cwd)
    else:
        os.chmod(cwd, 0o777)
    here = os.getcwd()
    os.chdir(cwd)
    try:
        root = os.path.join(d, 'out_rights')
        code, out = cli('--data', root, 'init', 'OUTRIGHTS', '--keys', keys)
        expect(code == 0 and 'WARN: the rights of' not in out, 'init in a current folder with other rights: %s' %
               out[-300:])
        code, out = cli('--data', root, 'backup', 'second.tfnbak')
        expect(code == 0 and 'WARN: the rights of' not in out, 'backup in a current folder with other rights: %s' %
               out[-300:])
        for n in ('OUTRIGHTS-recovery.tfnkey', 'OUTRIGHTS-backup.tfnbak', 'second.tfnbak'):
            p = os.path.join(cwd, n)
            if sys.platform == 'win32':
                others = pm_data.rights(p)[0]
                expect(os.path.isfile(p) and others == [], '%s: only the rights of this user (%r)' % (n, others))
            else:
                expect(stat.S_IMODE(os.stat(p).st_mode) == 0o600, '%s: the mode 0600' % n)
        expect(not [n for n in os.listdir(cwd) if n.endswith('.tmp')], 'no temporary file stays in the current '
               'folder')
    finally:
        os.chdir(here)


def plain_parts(root):
    """The data folder root with keys/ and pool/ of an older poolmgr: on Windows, folders with no mode, so they take
    the inherited rights of root (os.makedirs with a mode gives a folder its own rights from Python 3.12.4 on: the test
    must not depend on the version of Python)."""
    os.makedirs(root)
    if sys.platform == 'win32':
        for part in ('keys', 'pool'):
            os.mkdir(os.path.join(root, part))


def folder_old_parts(d):
    """A data folder of an older poolmgr: keys/ and pool/ have the rights of the folder (Windows: inherited rights of
    the group Users; Linux and macOS: the mode 0755). store key gives them their own rights before the key file goes
    into keys/; doctor names them before."""
    root = os.path.join(d, 'old_parts')
    data = pm_data.Data(root)
    plain_parts(root)
    if sys.platform == 'win32':
        r = subprocess.run([pm_data.win_tool('icacls.exe'), root, '/grant', '*S-1-5-32-545:(OI)(CI)RX'],
                           capture_output=True)
        expect(r.returncode == 0, 'icacls: a test right for the group Users on %s' % root)
    small_pool(data)
    if sys.platform != 'win32':
        for part in ('keys', 'pool'):
            os.chmod(os.path.join(root, part), 0o755)
    code, out = cli('--data', root, 'doctor')
    expect('the part keys/ of the data folder' in out and 'the part pool/ of the data folder' in out,
           'doctor: keys/ and pool/ with other rights (%s)' % out[-600:])
    code, out = cli('--data', root, 'store', 'key')
    expect(code == 0 and os.path.isfile(data.key_store), 'store key in a data folder of an older poolmgr: %s' %
           out[-300:])
    for rel in ('keys', os.path.join('keys', 'store_pool.pem'), 'pool'):
        p = os.path.join(root, rel)
        if sys.platform == 'win32':
            others = pm_data.rights(p)[0]
            expect(others == [], 'store key: other accounts have rights on %s: %r' % (rel, others))
        else:
            want = 0o700 if os.path.isdir(p) else 0o600
            expect(stat.S_IMODE(os.stat(p).st_mode) == want, 'store key: the mode of %s' % rel)
    # the first change lock of a command (push, remove, adopt, webpass new) does the same before its first write
    root2 = os.path.join(d, 'old_parts2')
    data2 = pm_data.Data(root2)
    plain_parts(root2)
    if sys.platform == 'win32':
        r = subprocess.run([pm_data.win_tool('icacls.exe'), root2, '/grant', '*S-1-5-32-545:(OI)(CI)RX'],
                           capture_output=True)
        expect(r.returncode == 0, 'icacls: a test right for the group Users on %s' % root2)
    small_pool(data2)
    if sys.platform != 'win32':
        for part in ('keys', 'pool'):
            os.chmod(os.path.join(root2, part), 0o755)
    expect(all(pm_data.rights(os.path.join(root2, part))[0] for part in ('keys', 'pool')),
           'an older data folder: keys/ and pool/ have other rights')
    with pm_data.Data(root2).change():
        pass
    for part in ('keys', 'pool'):
        others = pm_data.rights(os.path.join(root2, part))[0]
        expect(others == [], 'the change lock: other accounts have rights on %s: %r' % (part, others))


def folder_restore(d, root):
    """restore: the empty parts of an older poolmgr and its lock (and the task file of Windows) are not files of a
    pool; another file stops the restore."""
    bak = os.path.join(d, 'FOLDER-backup.tfnbak')
    parts = os.path.join(d, 'r_parts')
    for part in ('keys', 'pool', 'log', os.path.join('cache', 'fw'), os.path.join('cache', 'store'), 'service'):
        os.makedirs(os.path.join(parts, part))
    with open(os.path.join(parts, 'lock'), 'wb') as h:
        h.write(b'1234\n')
    with open(os.path.join(parts, 'service', 'tfn-poolmgr-task.xml'), 'wb') as h:
        h.write(b'<Task/>')
    code, out = cli('--data', parts, 'restore', bak)
    expect(code == 0 and pm_data.Data(parts).load() is not None and 'task file of an older' in out and
           not [n for n in os.listdir(parts) if n.startswith('.restore-')], 'restore into the empty parts of an older '
           'poolmgr: %s' % out[-300:])
    stray = os.path.join(d, 'r_stray')
    os.makedirs(os.path.join(stray, 'keys'))
    with open(os.path.join(stray, 'notes.txt'), 'wb') as h:
        h.write(b'x')
    fw_sign.keygen(os.path.join(stray, 'keys', 'admin_a.pem'))
    before = tree(stray)
    code, out = cli('--data', stray, 'restore', bak)
    expect(code == 2 and 'notes.txt' in out and 'keys' in out and tree(stray) == before, 'restore into a folder with '
           'other files: exit 2, the names, no change: %s' % out[-300:])


def folder_rights(d):
    """The rights: doctor shows the other accounts of a folder and the command that repairs it; the command repairs
    it. Windows: a user name with a space and a domain is one argument of icacls."""
    acl = os.path.join(d, 'acl')
    if sys.platform == 'win32':
        plain_parts(acl)
    small_pool(pm_data.Data(acl))
    if sys.platform == 'win32':
        r = subprocess.run([pm_data.win_tool('icacls.exe'), acl, '/grant', '*S-1-5-32-545:(OI)(CI)RX'],
                           capture_output=True)
        expect(r.returncode == 0, 'icacls: a test right for the group Users')
    else:
        os.chmod(acl, 0o755)
    code, out = cli('--data', acl, 'doctor')
    warn = [ln for ln in out.splitlines() if ln.startswith('WARN') and ('rights' in ln or 'mode' in ln)]
    if sys.platform == 'win32':
        # keys/ and pool/ have the inherited rights of the folder: doctor names them too
        expect(len(warn) == 6 and 'icacls' in warn[1] and 'the part keys/' in warn[2] and 'the part pool/' in warn[4],
               'doctor: the other accounts and the repair command of the folder, keys/ and pool/ (%r)' % warn)
    else:
        expect(len(warn) == 2 and 'chmod 700' in warn[1], 'doctor: the other accounts and the repair command (%r)' %
               warn)
    others, fix = pm_data.rights(acl)
    if sys.platform == 'win32':
        expect('Users' in others and '/inheritance:r' in fix and '/remove:g' in fix, 'rights(): %r %r' % (others, fix))
        folder_no_sid(acl)
        r = subprocess.run([pm_data.win_tool('icacls.exe')] + pm_data.grant_args(acl, ['S-1-5-32-545'])[1:],
                           capture_output=True)
    else:
        os.chmod(acl, 0o700)
    code, out = cli('--data', acl, 'doctor')
    expect(not [ln for ln in out.splitlines() if ln.startswith('WARN') and 'rights' in ln] and
           pm_data.rights(acl) == ([], ''), 'doctor after the repair: no WARN of the rights (%s)' % out[-300:])
    if sys.platform != 'win32':
        return
    # the name in place of the SID (whoami does not answer): "NT AUTHORITY\Authenticated Users" has a space and a
    # domain; the folder then has that group in place of the user
    named = os.path.join(d, 'acl named')
    os.makedirs(named)
    keep = (list(pm_data._SID), os.environ.get('USERDOMAIN'), os.environ.get('USERNAME'))
    pm_data._SID[:] = [None]
    os.environ['USERDOMAIN'], os.environ['USERNAME'] = 'NT AUTHORITY', 'Authenticated Users'
    try:
        err = pm_data.protect(named)
    finally:
        pm_data._SID[:] = keep[0]
        for k, v in (('USERDOMAIN', keep[1]), ('USERNAME', keep[2])):
            if v is None:
                os.environ.pop(k, None)
            else:
                os.environ[k] = v
    if 'No mapping' in err:
        print('(the name of the group is not English on this computer: the check of a name is not done)')
        return
    expect(err == '' and 'Authenticated Users' in pm_data.rights(named)[0], 'protect() with a name with a space and '
           'a domain: %r %r' % (err, pm_data.rights(named)))


def folder_no_sid(acl):
    """Windows, whoami gives no SID: rights() gives no repair command (a /remove:g of the SID of the own entry of the
    user would remove it), and the second WARN line tells why."""
    keep = list(pm_data._SID)
    pm_data._SID[:] = [None]
    try:
        others, fix = pm_data.rights(acl)
        lines = pm_data.rights_lines(acl)
    finally:
        pm_data._SID[:] = keep
    expect('Users' in others and fix == '' and len(lines) == 2 and 'SID of your user is not known' in lines[1] and
           '/remove:g' not in ' '.join(lines) and '/grant' not in ' '.join(lines),
           'rights() with no SID of this user, so no repair command (%r %r)' % (others, lines))


def folder_damaged(d):
    """A damaged config.json, pool.json or secrets.json: a command stops with exit 2 and names the file (no trace);
    doctor gives a FAIL line. A damaged webpass.json: exit 2, and the file stays as it is."""
    root = os.path.join(d, 'damaged')
    data = pm_data.Data(root)
    small_pool(data)
    data.save_cfg()
    for rel, bad in (('config.json', None), (os.path.join('pool', 'pool.json'), None),
                     (os.path.join('pool', 'secrets.json'), b'{"pool_k": "00"}')):
        path = os.path.join(root, rel)
        keep = open(path, 'rb').read()
        with open(path, 'wb') as h:
            h.write(keep[:len(keep) // 2] if bad is None else bad)
        try:
            code, out = cli('--data', root, 'list')
            expect(code == 2 and 'STOP: a file of the data folder is damaged' in out and os.path.basename(rel) in out
                   and 'restore' in out, 'list with a damaged %s: exit 2 (%s)' % (rel, out[-300:]))
            code, out = cli('--data', root, 'doctor')
            expect(code == 1 and 'FAIL the data folder' in out and 'is damaged' in out,
                   'doctor with a damaged %s: FAIL (%s)' % (rel, out[-300:]))
        finally:
            with open(path, 'wb') as h:
                h.write(keep)
    code, out = cli('--data', root, 'list')
    expect(code == 0 and 'pool SMALL' in out, 'list after the repair of the files (%s)' % out[-200:])
    # a config.json whose channel is text: exit 2 with the name of the file; addrs.json that is a list: no address
    keep = open(data.cfg_file, 'rb').read()
    cfg = json.loads(keep.decode())
    cfg['channel'] = 'https://example.org/'
    with open(data.cfg_file, 'wb') as h:
        h.write(json.dumps(cfg).encode())
    try:
        code, out = cli('--data', root, 'channel')
        expect(code == 2 and 'is damaged' in out and 'config.json' in out and '"channel"' in out and
               'Traceback' not in out, 'channel with a "channel" that is text in config.json: exit 2 (%s)' %
               out[-300:])
    finally:
        with open(data.cfg_file, 'wb') as h:
            h.write(keep)
    for bad in (b'[1, 2]', b'{"AABBCC000001": 7}', b'{"AABBCC000001": {"ip": 5, "t": 1}}',
                b'{"AABBCC000001": {"ip": "127.0.0.9", "t": "x"}}', b'{"AABBCC000001": {"ip": "127.0.0.9"}}'):
        with open(data.addrs_file, 'wb') as h:
            h.write(bad)
        code, out = cli('--data', root, 'list')
        expect(code == 0 and 'pool SMALL' in out and 'Traceback' not in out and data.addrs() == {},
               'list with addrs.json %r: no trace (%s)' % (bad, out[-300:]))
        code, out = cli('--data', root, 'webpass', 'show', '127.0.0.9')
        expect(code == 2 and 'no member 127.0.0.9' in out and 'Traceback' not in out,
               'a member at an address with addrs.json %r: a STOP line, no trace (%s)' % (bad, out[-300:]))
        data.set_addr('AABBCC000001', '127.0.0.9', '', '00' * 8)
        expect(data.addrs().get('AABBCC000001', {}).get('ip') == '127.0.0.9', 'set_addr over addrs.json %r' % bad)
    os.remove(data.addrs_file)
    wp = os.path.join(root, 'pool', 'webpass.json')
    with open(wp, 'wb') as h:
        h.write(b'{"aabbcc000001": ')
    try:
        code, out = cli('--data', root, 'webpass', 'show', 'AABBCC000001')
        expect(code == 2 and 'webpass.json is damaged' in out and open(wp, 'rb').read() == b'{"aabbcc000001": ',
               'a damaged webpass.json: exit 2, the file stays (%s)' % out[-200:])
        for argv in (('list', '--live'), ('adopt', '--all'), ('adopt', '127.0.0.9')):
            code, out = cli('--data', root, *argv)
            expect(code == 2 and 'STOP:' in out and 'webpass.json is damaged' in out and
                   open(wp, 'rb').read() == b'{"aabbcc000001": ', '%s with a damaged webpass.json: STOP, exit 2 '
                   '(%s)' % (' '.join(argv), out[-200:]))
    finally:
        os.remove(wp)
    # the store tokens and the friend entries that wait: a damaged file stops the program, and it stays as it is;
    # remove stops before its first change (the member GHOST has no board)
    p = data.load()
    gpk = fw_sign.keygen(os.path.join(d, 'ghost.pem'))
    p.upsert({'id': pc.device_id(gpk).hex(), 'pk': gpk.hex(), 'mac': 'aabbcc0000ee', 'role': pool.ROLE_MEMBER,
              'flags': 0, 'name': 'GHOST'})
    p.sign(data.key_a)
    p.save(data.pool_dir)
    data.load()
    for name, bad, fn in (('store_tok.json', b'{"AABBCC000001": "ab', lambda: pm_store.load_tokens(data)),
                          ('friends_out.json', b'[1, 2]', lambda: pm_pool.out_load(data)),
                          ('friends_out.json', b'{"format": 1, "pending": 7}',
                           lambda: pm_pool.out_add(data, {'mac': 'aabbcc000001'}, {'mac': 'aabbcc000002',
                                                                                   'name': 'GONE'}))):
        path = os.path.join(root, 'pool', name)
        with open(path, 'wb') as h:
            h.write(bad)
        try:
            try:
                fn()
                stopped = ''
            except poolmgr.InputError as e:
                stopped = str(e)
            expect('is damaged' in stopped and name in stopped and open(path, 'rb').read() == bad,
                   'a damaged %s: InputError with its name, the file stays (%r)' % (name, stopped))
            v = pm_data.Data(root).load().version
            code, out = cli('--data', root, 'remove', 'GHOST')
            expect(code == 2 and 'is damaged' in out and open(path, 'rb').read() == bad and
                   pm_data.Data(root).load().version == v and 'remove GHOST' not in out, 'remove with a damaged %s: '
                   'exit 2 before a change, the file stays (%s)' % (name, out[-200:]))
        finally:
            os.remove(path)


def folder_questions(d, root):
    """The pass phrase questions name their files; a wrong kind of file stops before the question."""
    os.environ.pop(poolmgr.PHRASE_ENV, None)
    asked = []
    real = pm_pool.ask_secret

    def fake(prompt=''):
        asked.append(prompt)
        return PHRASE
    pm_pool.ask_secret = fake
    try:
        for argv, words, n in ((['backup', 'q.tfnbak'], 'the new backup q.tfnbak', 2),
                               (['check-backup', 'q.tfnbak'], 'the backup q.tfnbak', 1),
                               (['check-backup', 'FOLDER-recovery.tfnkey'], 'the key file FOLDER-recovery.tfnkey '
                                                                            '(key R)', 1),
                               (['recover', '--recovery', 'FOLDER-recovery.tfnkey'], 'the key file '
                                                                                    'FOLDER-recovery.tfnkey (key R)',
                                1)):
            del asked[:]
            code, out = cli('--data', root, *argv)
            expect(code == 0 and len(asked) == n and all(words in a for a in asked), '%s: the question names the '
                   'file (%r %s)' % (' '.join(argv), asked, out[-200:]))
        del asked[:]
        code, out = cli('--data', os.path.join(d, 'r_new'), 'restore', 'q.tfnbak')
        expect(code == 0 and len(asked) == 1 and 'the backup q.tfnbak' in asked[0], 'restore: the question names the '
               'file (%r)' % asked)
        del asked[:]
        code, out = cli('--data', os.path.join(d, 'r_new2'), 'restore', 'FOLDER-recovery.tfnkey')
        expect(code == 2 and not asked and 'is a key file' in out, 'restore of a key file: a stop before the question')
        code, out = cli('--data', root, 'check-backup', os.path.join(d, 'fkeys', 'store.pem'))
        expect(code == 1 and not asked and 'not a .tfnkey or .tfnbak file' in out, 'check-backup of another file: a '
               'stop before the question')
    finally:
        pm_pool.ask_secret = real
        os.environ[poolmgr.PHRASE_ENV] = PHRASE


def step_flow(d):
    os.environ[poolmgr.PHRASE_ENV] = PHRASE
    pm_net.UDP_PORT = free_port(socket.SOCK_DGRAM)
    pm_pool.JOIN_LISTEN = 1.5
    pm_pool.open_console = pool_tool.open_console = lambda port: FakeConsole(CONS[port])
    pm_ota.POLL_S, pm_ota.WAIT_S = 0.1, 10.0
    pm_store.ALLOW_WAIT = 8
    f = Flow(d)
    try:
        for fn in (f.release, f.init, f.adopt, f.friends, f.store, f.ota, f.serve_round, f.remove, f.backup,
                   f.recover, f.recover_new_key):
            n = len(FAILS)
            fn()
            if len(FAILS) > n:
                print('the flow stops at %s' % fn.__name__)
                break
    finally:
        f.close()
    return f.done


def main():
    t0 = time.time()
    cwd = os.getcwd()
    free_family_ports()
    with tempfile.TemporaryDirectory(prefix='poolmgr_flow_') as d:
        os.chdir(d)
        try:
            step_program(d)
            done = step_flow(d)
            step_folder(d)
        finally:
            os.chdir(cwd)
            builtins.input = REAL_INPUT
    if FAILS:
        print('POOLMGR FLOW PY FAILED: %d of %d checks' % (len(FAILS), CHECKS[0]))
        return 1
    print('POOLMGR FLOW PY OK (%d checks, %d flow steps: %s; %.1f s)' % (CHECKS[0], len(done), '; '.join(done),
                                                                          time.time() - t0))
    return 0


if __name__ == '__main__':
    sys.exit(main())
