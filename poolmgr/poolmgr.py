#!/usr/bin/env python3
"""poolmgr: the pool manager of TFN OS for up to 16 boards on one computer (Windows, macOS, Linux, Raspberry Pi).

Python 3.9 or later, the standard library and the program "openssl" only (pyserial only for --console). The program
keeps one pool in a data folder (default ~/.tfn-poolmgr; --data or the variable TFN_POOLMGR_DATA). It opens no port
on the network, except "serve" (UDP 47474: listen only; TCP 47480: the local store) and a short UDP listen.

The start:
  python poolmgr/poolmgr.py doctor              the check of this computer
  python poolmgr/poolmgr.py init <pool name>    the pool, the keys, the first backup
  python poolmgr/poolmgr.py channel <file>      (a pool from a git checkout) channel.json of a release: the web site
  (on each board: SETUP > POOL > JOIN)
  python poolmgr/poolmgr.py adopt --all         each board joins (compare the 6 digits, tap ALLOW)

A member = its name, device ID, MAC or address. Exit 0 = ok, 1 = a board refused or a check failed, 2 = a fault of
the input or of the tool.
"""
import argparse
import json
import os
import socket
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import pm_shared  # noqa: E402
from pm_shared import Failed, InputError, NotDone, fw_sign, pool_crypto, pool_tool  # noqa: E402
import pm_board  # noqa: E402
import pm_data  # noqa: E402
import pm_keys  # noqa: E402
import pm_net  # noqa: E402
import pm_ota  # noqa: E402
import pm_pool  # noqa: E402
import pm_serve  # noqa: E402
import pm_service  # noqa: E402
import pm_store  # noqa: E402

MIN_PHRASE = pm_pool.MIN_PHRASE
PHRASE_ENV = pm_pool.PHRASE_ENV
RECOVER_HINT = 'poolmgr recover --recovery <the .tfnkey file of key R> makes a new key A'
SSL_HINT = 'install openssl, or set OPENSSL=<path>'


# ---------------------------------------------------------------- the output
def show(rows):
    """Print the rows of a command: a line of text, or a dict as "key=value" pairs."""
    for r in rows or ():
        if isinstance(r, dict):
            print('  '.join('%s=%s' % (k, r[k]) for k in r))
        else:
            print(r)


def ask_phrase(what, confirm=False):
    """The pass phrase of what (the name of the file): the variable TFN_POOLMGR_PHRASE (the tests), else a question
    (two times when confirm)."""
    return pm_pool.ask_phrase(what, confirm)


def read_file(path):
    try:
        with open(path, 'rb') as h:
            return h.read()
    except OSError as e:
        raise InputError('cannot read %s: %s' % (path, e.strerror))


def file_kind(path, want=None):
    """The bytes of a .tfnkey or .tfnbak file and its kind (pm_keys.KEY or BAK), before the question of the pass
    phrase. want: the kind that the command needs."""
    blob = read_file(path)
    kind = bytes(blob[:7])
    if kind not in pm_keys.KINDS:
        raise Failed('%s is not a .tfnkey or .tfnbak file of poolmgr' % path)
    if want == pm_keys.BAK and kind != want:
        raise InputError('%s is a key file (.tfnkey), not a backup (.tfnbak)' % path)
    if want == pm_keys.KEY and kind != want:
        raise InputError('%s is a backup (.tfnbak): give the recovery key file (.tfnkey)' % path)
    return blob, kind


def what_of(path, kind):
    return ('the key file %s (key R)' if kind == pm_keys.KEY else 'the backup %s') % path


# ---------------------------------------------------------------- doctor
def doctor(data):
    """The checks of this computer (design 2.4): the exit code."""
    bad = [0]

    def line(state, text):
        print('%-4s %s' % (state, text))
        if state == 'FAIL':
            bad[0] += 1

    v = sys.version_info
    line('OK' if v >= (3, 9) else 'FAIL', 'python %d.%d.%d (3.9 or later)' % (v[0], v[1], v[2]))
    line('OK', 'the shared modules: %s' % ' + '.join(pm_shared.FOLDERS))
    ssl_ok = False
    try:
        ssl = fw_sign.openssl()
        with tempfile.TemporaryDirectory() as t:
            pem = os.path.join(t, 'doctor.pem')
            pub = fw_sign.keygen(pem)
            msg = b'tfn-poolmgr doctor'
            ssl_ok = fw_sign.verify_raw(pub, msg, fw_sign.sign_raw(pem, msg))
        line('OK' if ssl_ok else 'FAIL', 'openssl %s: a test signature %s' % (ssl, 'verifies' if ssl_ok else
                                                                               'FAILS'))
    except (fw_sign.SignError, OSError) as e:
        line('FAIL', 'openssl: %s (%s)' % (e, SSL_HINT))
    try:
        import serial  # noqa: F401
        line('OK', 'pyserial: yes (--console works)')
    except ImportError:
        line('WARN', 'pyserial: no (only --console needs it: pip install pyserial)')
    for kind, port in ((socket.SOCK_DGRAM, pm_net.UDP_PORT), (socket.SOCK_STREAM, pm_serve.PORT)):
        name = '%s %d' % ('UDP' if kind == socket.SOCK_DGRAM else 'TCP', port)
        s = socket.socket(socket.AF_INET, kind)
        try:
            s.bind(('0.0.0.0', port))
            line('OK', 'the port %s is free' % name)
        except OSError:
            line('WARN', 'the port %s is in use (does "poolmgr serve" run?)' % name)
        finally:
            s.close()
    try:
        line('OK', 'the LAN address of this computer: %s' % pm_net.lan_ip_toward('192.0.2.1'))
    except OSError as e:
        line('WARN', 'the LAN address: none (%s)' % e)
    line('NOTE', 'Windows and macOS ask for a firewall rule at the first listen: allow the PRIVATE network only')
    doctor_folder(data, line, ssl_ok)
    doctor_channel(data, line)
    print('DOCTOR %s' % ('OK' if not bad[0] else 'FAILED (%d)' % bad[0]))
    return 1 if bad[0] else 0


def doctor_folder(data, line, ssl_ok=True):
    """The data folder: the pool, the rights of the folder, key A (ssl_ok: the openssl check of doctor passed).
    doctor changes nothing."""
    if not os.path.isdir(data.root):
        line('OK', 'the data folder %s: not there yet (init or restore makes it)' % data.root)
        return
    try:
        data.load()
    except (InputError, OSError, ValueError, KeyError) as e:
        line('FAIL', 'the data folder %s: %s' % (data.root, e))
        return
    if data.pool is None:
        line('OK', 'the data folder %s: no pool (a new pool: poolmgr init <name>; a pool from a backup: poolmgr '
             'restore <file>)' % data.root)
        left = pm_data.leftovers(data.root)
        if left:
            line('WARN', 'the data folder has %s but no pool: restore stops there%s; move these parts to another '
                 'place (poolmgr never deletes a key)' % (', '.join(left[:6]), ', and init too (key files)' if
                                                          'keys' in left else ''))
    else:
        line('OK', 'the data folder %s: pool %s' % (data.root, data.pool.name))
    for t in pm_data.all_rights_lines(data.root):
        line('WARN', t)
    if data.pool is not None:
        doctor_key_a(data, line, ssl_ok)


def doctor_key_a(data, line, ssl_ok=True):
    """keys/admin_a.pem must be the admin key A of the pool (else no change of the pool works). The read of the key
    needs openssl: with no good openssl (ssl_ok False) a fault of the read is a WARN with the openssl hint, not a
    damaged key."""
    if not os.path.isfile(data.key_a):
        line('FAIL', 'key A: %s is not there (%s)' % (data.key_a, RECOVER_HINT))
        return
    try:
        pub = pool_crypto.p256_pub(data.key_a)
    except (fw_sign.SignError, OSError, ValueError) as e:
        if ssl_ok:
            line('FAIL', 'key A: %s does not read (%s): %s' % (data.key_a, e, RECOVER_HINT))
        else:
            line('WARN', 'key A: not checked: openssl does not work (the openssl line above; %s)' % SSL_HINT)
        return
    if pub != data.pool.admins[0]:
        line('FAIL', 'key A: %s (key ID %s) is not the admin key A of the pool (key ID %s): %s' % (
            data.key_a, pool_crypto.key_id(pub).hex(), pool_crypto.key_id(data.pool.admins[0]).hex(), RECOVER_HINT))
        return
    line('OK', 'key A: key ID %s, the admin key A of the pool' % pool_crypto.key_id(pub).hex())


def doctor_channel(data, line):
    """The channel: the one of the pool (config.json) when the folder has a pool, else channel.json of the program
    (init copies it). One GET of the firmware index."""
    if data.pool is not None:
        ch, where, hint = data.cfg.get('channel') or {}, 'the channel of the pool', pm_shared.CHANNEL_HINT_POOL
    else:
        try:
            ch = json.loads(read_file(os.path.join(HERE, 'channel.json')).decode('utf-8'))
        except (InputError, ValueError):
            ch = {}
        where, hint = 'the channel', pm_shared.CHANNEL_HINT_NEW
    if not isinstance(ch, dict) or not ch.get('pages'):
        line('WARN', '%s: none, no web site (%s)' % (where, hint))
        return
    url = ch['pages'].rstrip('/') + '/firmware/index.json'
    try:
        idx = json.loads(pm_board.http_get(url, timeout=5).decode('utf-8'))
        line('OK', '%s %s: %d kinds' % (where, url, len(idx.get('kinds', idx))))
    except (Failed, ValueError, AttributeError, TypeError) as e:
        line('WARN', '%s %s: %s' % (where, url, e))


# ---------------------------------------------------------------- the commands
def cmd_doctor(a, data):
    return doctor(data)


def cmd_init(a, data):
    show(pm_pool.init(data, a.name, a.keys))


def cmd_channel(a, data):
    show(pm_pool.channel(data, a.file))


def cmd_find(a, data):
    joins, _ = pm_net.listen(data, a.secs)
    if not joins:
        print('no board with an open JOIN screen (on the board: SETUP > POOL > JOIN)')
    for j in joins:
        print('%-16s %s  %s' % (j.get('name', ''), j.get('mac', '')[-4:], j.get('ip', '')))


def cmd_adopt(a, data):
    show(pm_pool.adopt(data, ip=a.address, all_=a.all, console=a.console, digits=a.digits, name=a.name,
                       role=a.role, webpass=not a.no_webpass, friends=not a.no_friends, password=a.password))


def cmd_list(a, data):
    show(pm_pool.list_members(data, live=a.live))


def cmd_push(a, data):
    show(pm_pool.push(data, a.members))


def cmd_remove(a, data):
    show(pm_pool.remove(data, a.member, keep_webpass=a.keep_webpass))


def cmd_friends(a, data):
    show(pm_pool.friends(data, check=a.check))


def cmd_webpass(a, data):
    if a.op == 'show':
        show(pm_pool.webpass(data, 'show', [a.member]))
    else:
        show(pm_pool.webpass(data, 'new', a.members))


def cmd_update(a, data):
    w = list(a.members)
    if w[:1] == ['check']:
        show(pm_ota.plan(data, w[1:], a.version))
        return
    if not w and not a.all:
        raise InputError('give the members, or --all')
    show(pm_ota.update(data, [] if a.all else w, a.version, a.now))


def cmd_store(a, data):
    if a.op in ('official', 'local'):
        show(pm_store.set_mode(data, a.op))
    elif a.op == 'sync':
        print('%d packages in the mirror' % pm_store.sync(data))
    elif a.op == 'key':
        print('the pool store key: %s' % pm_store.make_key(data))
    elif a.op == 'trust':
        show(pm_store.trust(data, a.members, a.console))
    else:
        show(pm_store.publish(data, a.folder))


def cmd_backup(a, data):
    data.need_pool()
    pm_keys.backup_target(a.file)   # (before the question: a wrong file name stops at once)
    show(pm_keys.backup(data, a.file, ask_phrase('the new backup %s' % a.file, True), a.replace))


def cmd_restore(a, data):
    pm_keys.restore_check(data.root)
    blob, kind = file_kind(a.file, pm_keys.BAK)
    show(pm_keys.restore(blob, data.root, ask_phrase(what_of(a.file, kind))))


def cmd_check_backup(a, data):
    blob, kind = file_kind(a.file)
    show(pm_keys.check(blob, ask_phrase(what_of(a.file, kind)), data))


def cmd_recover(a, data):
    data.need_pool()
    blob, kind = file_kind(a.recovery, pm_keys.KEY)
    show(pm_keys.recover(data, blob, ask_phrase(what_of(a.recovery, kind)), a.new_key))


def cmd_serve(a, data):
    if a.openssl:
        os.environ['OPENSSL'] = a.openssl
    return pm_serve.run(data, a.port)


def cmd_service(a, data):
    return pm_service.command(a, data, os.path.abspath(__file__))


# ---------------------------------------------------------------- the arguments
def parser():
    ap = argparse.ArgumentParser(prog='poolmgr', description='The pool manager of TFN OS (one pool for each data '
                                 'folder).', epilog='Exit 0 = ok, 1 = a board refused or a check failed, 2 = a fault '
                                 'of the input or of the tool.')
    ap.add_argument('--data', help='the data folder (default: TFN_POOLMGR_DATA, else ~/.tfn-poolmgr)')
    sub = ap.add_subparsers(dest='cmd', metavar='<command>')
    sub.required = True

    def cmd(name, fn, text):
        p = sub.add_parser(name, help=text, description=text)
        p.set_defaults(fn=fn)
        return p

    cmd('doctor', cmd_doctor, 'check this computer: python, openssl, pyserial, the ports, the LAN address, the data '
        'folder (its rights, key A) and the channel')
    p = cmd('init', cmd_init, 'make the pool: keys A and R, the manager member, the first backup')
    p.add_argument('name', help='the name of the pool')
    p.add_argument('--keys', metavar='CHANNEL_JSON', help='channel.json of a GitHub release of tfn-os (the web site, '
                   'the repo and the official keys), when the program folder has no channel.json (a git checkout); '
                   'a tfn-keys.json gives only the keys (no update, no store sync)')
    p = cmd('channel', cmd_channel, 'show the channel of the pool (the web site of the releases, the repo, the '
            'official keys), or set it from channel.json of a GitHub release of tfn-os: the official keys of the '
            'file must be the same as the keys of the pool')
    p.add_argument('file', nargs='?', metavar='CHANNEL_JSON', help='channel.json of a GitHub release of tfn-os (the '
                   'release zip has poolmgr/channel.json; none: show the channel of the pool)')
    p = cmd('find', cmd_find, 'list the boards with an open JOIN screen (SETUP > POOL > JOIN)')
    p.add_argument('--secs', type=float, default=10.0, help='the listen time (default 10 s)')
    p = cmd('adopt', cmd_adopt, 'a board joins the pool with the 6 digits')
    p.add_argument('address', nargs='?', help='the address of the board (default: listen for the JOIN screens)')
    p.add_argument('--all', action='store_true', help='each board with an open JOIN screen, in turn')
    p.add_argument('--console', metavar='PORT', help='the USB console of the board: the program reads the 6 digits '
                   'and sends "pool allow"')
    p.add_argument('--digits', metavar='NNNNNN', help=argparse.SUPPRESS)   # (the tests: the digits of a fake board)
    p.add_argument('--password', nargs='?', const='', metavar='PW', help='the web password of a board that has one '
                   '(SETUP > WEB PASSWORD). Give it with no value: the program asks for it (no echo; give the address '
                   'before this option). A value goes into the history of the shell and into the process list. The '
                   'board keeps its own web password')
    p.add_argument('--name', help='the member name (16 bytes at most; default: the board name)')
    p.add_argument('--role', choices=('member', 'child'), default='member', help='the role (default member)')
    p.add_argument('--no-webpass', action='store_true', help='keep the web password of the board as it is')
    p.add_argument('--no-friends', action='store_true', help='do not make the members friends')
    p = cmd('list', cmd_list, 'the members, their addresses, the roster version and the key epoch')
    p.add_argument('--live', action='store_true', help='also the state, the firmware, the kind, the open app and the '
                   'store (official or local) of each board')
    p = cmd('push', cmd_push, 'send the newest roster and key to the members (all when none is given)')
    p.add_argument('members', nargs='*', help='the members')
    p = cmd('remove', cmd_remove, 'remove a member: the store, the friends, its web password of the pool, a new pool '
            'key')
    p.add_argument('member', help='the member')
    p.add_argument('--keep-webpass', action='store_true', help='the board keeps the web password of the pool '
                   '(default: it goes off; "webpass show <MAC>" prints a kept one)')
    p = cmd('friends', cmd_friends, 'make each member a friend of each other member (exit 1 when an entry is missing)')
    p.add_argument('--check', action='store_true', help='read the friend list of each member and add only the '
                   'missing entries')
    p = cmd('webpass', cmd_webpass, 'the web passwords of the members (for the web pages of the boards)')
    ws = p.add_subparsers(dest='op', metavar='<show|new>')
    ws.required = True
    q = ws.add_parser('show', help='print the web password of a member (or of the MAC of a board that left the pool)')
    q.add_argument('member', help='the member, or the MAC of a board that left the pool')
    q = ws.add_parser('new', help='a new random web password (all members when none is given)')
    q.add_argument('members', nargs='*', help='the members')
    p = cmd('update', cmd_update, 'the firmware of the public releases: "update check [<member>...]" lists, '
            '"update <member>... | --all" updates one board at a time')
    p.add_argument('members', nargs='*', help='"check", then the members; or the members to update')
    p.add_argument('--all', action='store_true', help='all members')
    p.add_argument('--version', help='a pinned version (the files of the GitHub release vV)')
    p.add_argument('--now', action='store_true', help='also a board with an open app (default: only at the menu)')
    p = cmd('store', cmd_store, 'the store of the pool: official, local, sync, key, trust, publish')
    ss = p.add_subparsers(dest='op', metavar='<official|local|sync|key|trust|publish>')
    ss.required = True
    ss.add_parser('official', help='the boards read the official static store (the default)')
    ss.add_parser('local', help='the boards read the local store of "serve" on this computer')
    ss.add_parser('sync', help='make the mirror of the official store (the local store)')
    ss.add_parser('key', help='make the pool store key (for the pool\'s own apps)')
    q = ss.add_parser('trust', help='send the pool store key to the members (tap ALLOW on each board)')
    q.add_argument('members', nargs='*', help='the members (all when none is given)')
    q.add_argument('--console', metavar='PORT', help='the USB console: the program sends "pub allow <key ID>"')
    q = ss.add_parser('publish', help='sign a package of "tfn_app.py build" with the pool store key and add it')
    q.add_argument('folder', help='the package folder')
    p = cmd('backup', cmd_backup, 'write the encrypted backup of the pool (.tfnbak; an older backup of the pool with '
            'the same pass phrase is replaced)')
    p.add_argument('file', help='the backup file')
    p.add_argument('--replace', action='store_true', help='replace the file also when it is a newer or different '
                   'backup of this pool (a higher roster version or key epoch, another key A, or parts that the data '
                   'folder does not have), the backup of another pool, or a backup that the new pass phrase does not '
                   'open')
    p = cmd('restore', cmd_restore, 'restore a backup into a new data folder (or one of poolmgr with no pool)')
    p.add_argument('file', help='the backup file')
    p = cmd('check-backup', cmd_check_backup, 'check a backup (.tfnbak; and compare it with the data folder) or a '
            'recovery key file (.tfnkey)')
    p.add_argument('file', help='the file')
    p = cmd('recover', cmd_recover, 'key A is lost or out: a new key A, a new pool key, new request keys')
    p.add_argument('--recovery', required=True, metavar='FILE', help='the recovery key file (.tfnkey)')
    p.add_argument('--new-key', action='store_true', help='make a new key A also when boards of the last recover '
                   'wait (the computer with the key A of the last recover is lost or stolen); the boards that wait '
                   'get it too')
    p = cmd('serve', cmd_serve, 'listen to the beacons, keep the members up to date, serve the local store (the lines '
            'also go into log/serve.log)')
    p.add_argument('--port', type=int, default=pm_serve.PORT, help='the TCP port of the local store (default %d)'
                   % pm_serve.PORT)
    p.add_argument('--openssl', metavar='PATH', help='the openssl program for serve (the Windows service gives the '
                   'variable OPENSSL here; default: OPENSSL, else openssl on the PATH, else Git for Windows)')
    p = cmd('service', cmd_service, 'the service that runs "serve" at each logon of your user: show (the file and the '
            'steps), install, remove')
    pm_service.add_args(p)
    return ap


def main(argv=None):
    a = parser().parse_args(argv)
    data = pm_data.Data(a.data)
    try:
        r = a.fn(a, data)
        return r if isinstance(r, int) else 0
    except NotDone as e:
        print(e)
        return 2
    except (Failed, pool_tool.Refused) as e:
        print('FAILED: %s' % e)
        return 1
    except (InputError, pool_tool.ToolError, fw_sign.SignError, pm_shared.pool.PoolError, OSError) as e:
        print('STOP: %s' % e)
        return 2
    except KeyboardInterrupt:
        print('STOP: interrupted')
        return 2
    except EOFError:
        print('STOP: the input ended before the answer to a question (do the command in a terminal)')
        return 2


if __name__ == '__main__':
    sys.exit(main())
