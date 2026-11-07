"""The pool commands of poolmgr (design 2.5-2.9): init, adopt, list, push, remove, friends, webpass, channel, and the
upkeep of one member (serve).

The functions print their progress lines and give the last lines (poolmgr.py prints them). They raise
pm_shared.Failed (exit 1) for a refusal or a failed check, InputError (exit 2) for a fault of the input. Each change
of the pool takes the change lock of the data folder (data.change()) and reads the pool again from its folder. A
command on a data folder with no pool stops before it makes or changes a file there.

The pool folder data.pool_dir has the form of pool_tool.py (pool.json, secrets.json, roster.bin, webpass.json), so
pool_tool.py and tests/pool/gate.py work on it with --dir <data>/pool --key <data>/keys/admin_a.pem. Two more files
are there: store_tok.json (pm_store.py) and friends_out.json (the friend entries that wait for their removal: a member
that did not answer "remove" keeps the MAC of the removed board; push, friends and the upkeep of serve try again).

The web passwords (webpass.json, MAC -> password): poolmgr gives a random web password to a member at the adopt, only
when the board has no web password of its own. "remove" turns that password off before the board leaves (or keeps
it: --keep-webpass). When the board does not answer, or after LEAVE on the board, the entry stays in webpass.json:
"webpass show <MAC>" prints it, and a new adopt of the board uses it.
"""
import getpass
import json
import os
import re
import secrets
import sys
import time
import urllib.parse

import pm_board
import pm_data
import pm_keys
import pm_net
import pm_store
from pm_shared import CHANNEL_HINT_POOL, Failed, InputError, pool, pool_crypto, pool_tool

HERE = os.path.dirname(os.path.abspath(__file__))
DONE = True

PHRASE_ENV = 'TFN_POOLMGR_PHRASE'
MIN_PHRASE = 12
JOIN_LISTEN = 10.0      # the listen for the 'A' 'J' packets of the boards with an open JOIN screen
ALLOW_WAIT = 120.0      # the wait for the tap on ALLOW (op=set answers "wait" until the tap)
SAS_WAIT = 20.0         # the wait for the 6 digits on the USB console
UPKEEP_S = 60.0         # the upkeep sends to one member at most once in this time
FRIENDS_MAX = 16
WEB_TRIES = 3           # the web passwords that a person can type for one board
OUT_FILE = 'friends_out.json'
open_console = pool_tool.open_console   # (the tests give a fake console here)
_fix_t = {}

WEB_HINT = ('%s has a web password (SETUP > WEB PASSWORD on the board): adopt <address> --password (the program asks '
            'for it), or remove the web password on the board')
NOT_RIGHT = '%s: the web password is not right (the web password of SETUP > WEB PASSWORD on the board)'
KEPT_HINT = ('%s: this pool keeps the web password of a board with this MAC (the board left the pool). poolmgr sends '
             'it only to an address that you give: poolmgr adopt %s')
NO_TERMINAL = ('no terminal for the question "%s": do the command in a terminal (a script: the variable %s gives a '
               'pass phrase, and "adopt <address> --password <password>" gives a web password)')
NO_TAP = ('nobody tapped ALLOW on the board in %d s: on the board open SETUP > POOL > JOIN again, then do the adopt '
          'again')
ENDED = ('the board ended the JOIN (NO on the board, or no ALLOW in 2 minutes): on the board open SETUP > POOL > JOIN '
         'again, then do the adopt again')
# the codes of the refusals that need more words than the board gives
CODE_TEXT = {'login': 'the board has a web password, and it did not take the request header of the pool'}
# the results of web_now() that are not the web password of the pool
WEB_NOW = {'none': 'it has no web password now', 'other': 'it asks for another web password'}


def say(text):
    print(text)
    if sys.stdout is not None:
        sys.stdout.flush()


# ---------------------------------------------------------------- the small parts
def ask_secret(prompt):
    """A question with no echo to the person at the terminal (the tests give a fake). InputError when nobody can
    answer it: no terminal, or the end of the input."""
    stop = NO_TERMINAL % (prompt.strip().rstrip(':'), PHRASE_ENV)
    if not can_ask():
        raise InputError(stop)
    try:
        return getpass.getpass(prompt)
    except EOFError:
        raise InputError(stop)


def can_ask():
    """True when a person can answer a question: the input is a terminal (on Linux and macOS also the terminal of the
    process, /dev/tty, which getpass reads)."""
    try:
        if sys.stdin is not None and sys.stdin.isatty():
            return True
    except (AttributeError, ValueError):
        pass
    if os.name == 'nt':
        return False
    try:
        os.close(os.open('/dev/tty', os.O_RDWR | getattr(os, 'O_NOCTTY', 0)))
        return True
    except OSError:
        return False


def ask_phrase(what, confirm=True):
    """The pass phrase of what (for example "the backup OUR_HOME-backup.tfnbak"): the variable TFN_POOLMGR_PHRASE
    (the tests and the scripts), else a question (two times when confirm)."""
    p = os.environ.get(PHRASE_ENV)
    if p is None:
        p = ask_secret('The pass phrase of %s (%d characters or more): ' % (what, MIN_PHRASE))
        if confirm and ask_secret('The pass phrase of %s again: ' % what) != p:
            raise InputError('the two pass phrases of %s are not the same' % what)
    if len(p) < MIN_PHRASE:
        raise InputError('the pass phrase of %s must have %d characters or more' % (what, MIN_PHRASE))
    return p


def file_name(name, pool_id=None):
    """A file name from the pool name: letters, digits, '-' and '_' only. A name with no ASCII letter or digit gives
    "pool-" and 8 hex digits of the pool ID."""
    s = re.sub(r'[^A-Za-z0-9_-]+', '_', name).strip('_')
    if re.search(r'[A-Za-z0-9]', s):
        return s
    return 'pool-%s' % pool_id.hex()[:8] if pool_id else 'pool'


def backup_name(p):
    return '%s-backup.tfnbak' % file_name(p.name, p.pool_id)


def backup_old(p):
    say('BACKUP IS OLD: poolmgr backup %s' % backup_name(p))


def save_webpass(data, wp):
    pool.write_file(os.path.join(data.pool_dir, 'webpass.json'), (json.dumps(wp, indent=1) + '\n').encode(), 0o600)


def mac_text(m):
    return m['mac'].upper()


def mac_of(text):
    """The MAC in a text (12 hex digits, with or with no ":", "-" or "." between them) in small hex letters, or
    ''."""
    t = str(text or '')
    h = re.sub(r'[^0-9a-fA-F]', '', t).lower()
    return h if len(h) == 12 and re.match(r'^[0-9a-fA-F:.-]+$', t) else ''


def load_locked(data):
    """The pool, read again in the change lock (another process can have changed it)."""
    data.pool = None
    return data.need_pool()


def key_a(data, p):
    if not os.path.isfile(data.key_a):
        raise InputError('no key A (%s): poolmgr recover --recovery <the .tfnkey file of key R> makes a new key A' %
                         data.key_a)
    if pool_crypto.p256_pub(data.key_a) != p.admins[0]:
        raise InputError('the key %s is not the admin key A of the pool %s: poolmgr recover --recovery <the .tfnkey '
                         'file of key R> makes a new key A' % (data.key_a, p.name))
    return data.key_a


def why(e):
    """The text of a fault: for a refusal of a board, its code and its words."""
    if isinstance(e, pool_tool.Refused):
        code = e.ans.get('error') or e.ans.get('code') or e.code
        text = e.ans.get('msg') or CODE_TEXT.get(code)
        return 'refused: %s%s' % (code, ' (%s)' % text if text else '')
    return str(e)


def key_ids(ch):
    """The lines of the key IDs of a channel (init and channel print them)."""
    out = []
    if ch.get('store_pub'):
        out.append('  store key %s' % pool_crypto.key_id(bytes.fromhex(ch['store_pub'])).hex())
    for k in ch.get('ota_pub') or ():
        out.append('  OTA key   %s' % pool_crypto.key_id(bytes.fromhex(k)).hex())
    return out


def key_id_text(keys):
    """The key IDs of public keys in hex, for a message ("none" for an empty or bad value)."""
    out = []
    for k in keys:
        try:
            out.append(pool_crypto.key_id(bytes.fromhex(k)).hex() if k else 'none')
        except ValueError:
            out.append('none')
    return ', '.join(out)


# ---------------------------------------------------------------- the channel (design 2.5 step 2, decision 9)
INIT_KEYS = 'give --keys <channel.json> of a GitHub release of tfn-os'
CHANNEL_KEYS = 'give the file: poolmgr channel <channel.json of a GitHub release of tfn-os>'


def community_keys(keys_file, how):
    """The channel of config.json: from the file keys_file (channel.json of a GitHub release; a tfn-keys.json of a
    release gives only the keys, with no web site and no repo), else from channel.json of the program folder (the
    release zip). how: the way to give the file on the command line of the caller (INIT_KEYS, CHANNEL_KEYS)."""
    if keys_file and not os.path.isfile(keys_file):
        raise InputError('no file %s: %s' % (keys_file, how))
    path = keys_file or os.path.join(HERE, 'channel.json')
    if not os.path.isfile(path):
        raise InputError('the program folder has no channel.json (a git checkout): %s' % how)
    try:
        with open(path, 'rb') as h:
            j = json.loads(h.read().decode('utf-8'))
        if 'store_pub' in j:
            store, ota = j['store_pub'], list(j.get('ota_pub') or [])
        else:
            store = j['store']['pubkey']
            ota = [j['ota']['pubkey']] + ([j['ota_backup']['pubkey']] if j.get('ota_backup') else [])
        pubs = [bytes.fromhex(x) for x in [store] + ota]
    except (OSError, ValueError, KeyError, TypeError) as e:
        raise InputError('%s: not a tfn-keys.json or channel.json (%s)' % (path, e))
    if not ota or any(len(k) != 65 or k[0] != 4 for k in pubs):
        raise InputError('%s: each key must be a P-256 public key (65 bytes, 04...) and one OTA key at least' % path)
    ch = {'format': 1, 'pages': j.get('pages', ''), 'repo': j.get('repo', ''), 'store_pub': store.lower(),
          'ota_pub': [k.lower() for k in ota]}
    if isinstance(j.get('releases'), str) and j['releases'].startswith(('https://', 'http://')):
        ch['releases'] = j['releases']   # (optional: another place of the release files vV/)
    return ch, path


def channel(data, path=None):
    """The command "channel". With no file: the channel of the pool (config.json "channel"). With a file (channel.json
    of a GitHub release): it becomes the channel of the pool (the web site, the repo, the place of the release files).
    The official keys of a pool never change: the store key and the OTA keys of the file must be the same as the keys
    of the pool (init wrote them)."""
    p = data.need_pool()
    if not path:
        ch = data.cfg.get('channel') or {}
        return ['the channel of the pool %s (%s):' % (p.name, data.cfg_file)] + channel_lines(ch)
    new, src = community_keys(path, CHANNEL_KEYS)
    if not str(new['pages']).startswith(('https://', 'http://')):
        raise InputError('%s has no web site of the releases ("pages"): it has only the keys (a tfn-keys.json); give '
                         'channel.json of a GitHub release of tfn-os' % src)
    with data.change():
        p = load_locked(data)
        old = data.cfg.get('channel') or {}
        have = [str(old.get('store_pub', '')).lower()] + sorted(str(k).lower() for k in old.get('ota_pub') or [])
        want = [new['store_pub']] + sorted(new['ota_pub'])
        if have != want:
            raise InputError('%s has other official keys (store, OTA: %s) than the pool %s (%s): a channel never '
                             'changes the keys of a pool (another release key: another pool, --data <folder>)' %
                             (src, key_id_text(want), p.name, key_id_text(have)))
        data.cfg['channel'] = new
        data.save_cfg()
        data.log('channel %s: the web site %s, the repo %s' % (os.path.basename(src), new['pages'],
                                                                new['repo'] or '-'))
    return (['the new channel of the pool %s (from %s): the official keys are the same as the keys of the pool' %
             (p.name, src)] + channel_lines(new) +
            ['BACKUP IS OLD: poolmgr backup %s' % backup_name(p), 'The next step: poolmgr update check'])


def channel_lines(ch):
    site = ch.get('pages') or 'none: update and store sync do not work (%s)' % CHANNEL_HINT_POOL
    out = ['  web site  %s' % site, '  repo      %s' % (ch.get('repo') or 'none')]
    if ch.get('releases'):
        out.append('  releases  %s' % ch['releases'])
    return out + key_ids(ch)


# ---------------------------------------------------------------- init (design 2.5)
def pub_bytes(v):
    """A public value of pm_keys (bytes or hex text) as 65 bytes, or None."""
    if isinstance(v, str):
        v = bytes.fromhex(v)
    return bytes(v) if v else None


def init_check(data):
    """init needs a data folder with no pool and no key: a key is never deleted or overwritten."""
    if data.has_pool():
        raise InputError('%s has a pool already: a pool is never overwritten (another pool: --data <folder>)' %
                         data.root)
    if os.path.exists(data.root) and not os.path.isdir(data.root):
        raise InputError('%s is a file, not a folder' % data.root)
    keys = os.path.join(data.root, 'keys')
    have = sorted(os.listdir(keys)) if os.path.isdir(keys) else []
    if have:
        raise InputError('%s has key files but no pool (%s): poolmgr never deletes or overwrites a key; move the '
                         'folder to another place, or give another folder with --data' % (
                             data.root, ', '.join('keys/' + n for n in have)))
    if os.path.isdir(data.pool_dir) and os.listdir(data.pool_dir):
        raise InputError('%s has files in pool/ but no pool.json (a damaged pool folder): move the folder to another '
                         'place, or give another folder with --data' % data.root)


def init(data, name, keys_file=None):
    """The pool, keys A and R, the first backup (design 2.5). The program asks both pass phrases first. Then it makes
    all parts in a temporary folder in the data folder and moves them into place at the end (pool/ last). A fault on
    the way (also Ctrl+C) removes only the parts that this init made: a new init can start again."""
    if not 1 <= len(name.encode('utf-8')) <= pool.NAME_MAX:
        raise InputError('the pool name has 1 to %d bytes' % pool.NAME_MAX)
    init_check(data)
    channel_, src = community_keys(keys_file, INIT_KEYS)
    pid = pool_crypto.rand(16)
    base = file_name(name, pid)
    rec_out = os.path.abspath('%s-recovery.tfnkey' % base)
    bak_out = os.path.abspath('%s-backup.tfnbak' % base)
    for f in (rec_out, bak_out):
        if os.path.exists(f):
            raise InputError('%s is there already: a key file or a backup is never overwritten' % f)
    say('the official keys (%s): compare the key IDs with the release page of tfn-os' % src)
    for ln in key_ids(channel_):
        say(ln)
    if not channel_['pages']:
        say('WARN: %s has only the keys (no web site, no repo): update and store sync need channel.json of the '
            'release. Stop now with Ctrl+C and give --keys <channel.json>, or later: poolmgr channel '
            '<channel.json>' % src)
    r_phrase = ask_phrase('the key file %s (key R)' % rec_out)
    b_phrase = ask_phrase('the backup %s' % bak_out)
    made = data.make_root()
    try:
        with data.change(new=True):
            init_check(data)
            p, log = build(data, name, pid, channel_, (rec_out, bak_out), (r_phrase, b_phrase))
    except BaseException:
        if made:
            pm_data.drop_root(data.root)
        raise
    data.pool = p
    data.make()
    if log:
        with open(data.log_file, 'ab') as h:
            h.write(log)
    say('pool %s: ID %s, roster version %d, the data folder %s' % (p.name, p.pool_id.hex(), p.version, data.root))
    say('the first backup %s: pool %s, roster version %d, epoch %d' % (bak_out, p.name, p.version, p.epoch))
    for ln in pm_data.rights_lines(data.root):
        say('WARN: ' + ln)
    return ['COPY %s TO A USB STICK OR ANOTHER COMPUTER (the way back when this computer is lost or stolen); '
            'keep %s there too' % (rec_out, bak_out),
            'The next step: on each board SETUP > POOL > JOIN. Then: poolmgr adopt --all']


def build(data, name, pid, channel_, outs, phrases):
    """The parts of init in a temporary folder of the data folder (keys/ and pool/ with their own rights before the
    first key file: pm_data.private_tmp), then the move into place (commit()). Gives (the pool, the log lines of the
    temporary folder)."""
    rec_out, bak_out = outs
    tmp = pm_data.private_tmp(data.root, '.init-')
    try:
        t = pm_data.Data(tmp)
        t.make()
        t.cfg['channel'] = channel_
        t.save_cfg()
        a_pub = pub_bytes(pm_keys.make_admin(t)) or pool_crypto.p256_pub(t.key_a)
        say('key A (%s): key ID %s' % (data.key_a, pool_crypto.key_id(a_pub).hex()))
        tmp_rec = os.path.join(tmp, os.path.basename(rec_out))
        r_pub = pub_bytes(pm_keys.make_recovery(t, tmp_rec, phrases[0]))
        if r_pub is None:
            raise InputError('the recovery key gave no public value')
        rblob = pm_keys.read(tmp_rec)
        for ln in pm_keys.check(rblob, phrases[0], None) or ():
            say('  %s' % ln)
        say('key R: key ID %s, only in the encrypted file %s' % (pool_crypto.key_id(r_pub).hex(), rec_out))
        p = pool.Pool.create(name, a_pub, r_pub, 0)
        p.pool_id = pid
        p.add_manager()
        p.sign(t.key_a)
        p.save(t.pool_dir)
        t.log('init %s: pool %s, key A %s, key R %s' % (name, p.pool_id.hex(), pool_crypto.key_id(a_pub).hex(),
                                                        pool_crypto.key_id(r_pub).hex()))
        tmp_bak = os.path.join(tmp, os.path.basename(bak_out))
        pm_keys.backup(t, tmp_bak, phrases[1])
        commit(data, tmp, outs, rblob, pm_keys.read(tmp_bak))
        log = pm_keys.read(t.log_file) if os.path.isfile(t.log_file) else b''
    finally:
        if not pm_data.remove_tree(tmp):
            say('NOTE: the temporary folder %s stays (delete it: it has copies of the keys of init)' % tmp)
    return p, log


def commit(data, tmp, outs, rblob, bblob):
    """The end of init: the key file of R and the backup into the current folder (each with only the rights of this
    user, SYSTEM and the administrators: a WARN line when they do not set), then config.json, keys/ and pool/ of the
    temporary folder into the data folder (pool/ last: then the pool is there; a config.json of an older init that
    stopped, with no pool, is replaced). A fault moves the parts back and removes the two files that it wrote."""
    done = []
    try:
        for path, blob in zip(outs, (rblob, bblob)):
            err = pm_keys.write_new(path, blob)
            done.append(path)
            if err:
                say(pm_data.file_warn(path, err))
        pm_data.move_parts(data.root, tmp, ('config.json', 'keys', 'pool'))
    except BaseException:
        for path in done:
            try:
                os.remove(path)
            except OSError:
                pass
        raise


# ---------------------------------------------------------------- adopt (design 2.6)
def adopt(data, ip=None, all_=False, console=None, digits=None, name=None, role='member', webpass=True,
          friends=True, password=None):
    """The JOIN of a board with the 6 digits (design 2.6), then its web password and the friends. console: one USB
    console port, or a list of ports separated by commas (each board on its own console: the program finds the
    console that shows the 6 digits of the board). password: the web password of a board that has one ('' = the
    program asks for it); a board with a web password of its own keeps it. With more boards, a fault of one board
    (also an InputError: the end of the input at a question, a full pool) stops only the JOIN of that board."""
    pm_board.the_pool(data)
    pool_tool.load_webpass(data.pool_dir)   # (a damaged file stops the command here, before the first board)
    if name is not None and not 1 <= len(name.encode('utf-8')) <= pool.MEMBER_NAME:
        raise InputError('the member name has 1 to %d bytes' % pool.MEMBER_NAME)
    if role not in ('member', 'child'):
        raise InputError('the role is member or child')
    ports = [c.strip() for c in console.split(',') if c.strip()] if console else []
    cons = [(c, open_console(c)) for c in ports]
    done, bad, targets, end = [], 0, [], False
    try:
        for _, con in cons:
            con.write_line('pool join')
        if ip:
            targets = [{'ip': ip, 'name': '', 'mac': '', 'given': True}]
        else:
            targets = find_joins(data, all_)
        if len(targets) > 1 and (digits or name):
            raise InputError('--name is for one board' if name else '--digits is for one board')
        for t in targets:
            try:
                done.append(join_one(data, t, cons, digits, name or t.get('name') or None, role, password))
            except (pool_tool.Refused, pool_tool.ToolError, Failed, InputError) as e:
                if len(targets) == 1 and isinstance(e, InputError):
                    raise
                bad += 1
                say('%s: THE JOIN STOPPED: %s' % (t['ip'], why(e)))
                data.log('adopt %s: stopped: %s' % (t['ip'], why(e)))
                if len(targets) == 1:
                    raise Failed('the JOIN of %s stopped: %s' % (t['ip'], why(e)))
        end = True
    finally:
        for _, con in cons:
            con.close()
        if done and not end:
            adopt_stopped(data, done)
    if not done:
        raise Failed('no board joined')
    for m, own in done:
        after_join(data, m, webpass, friends, own)
    backup_old(data.pool)
    if bad:
        raise Failed('%d of %d boards did not join' % (bad, len(targets)))
    return ['%d board(s) joined the pool %s: %s' % (len(done), data.pool.name, ', '.join(m['name'] for m, _ in done))]


def adopt_stopped(data, done):
    """The adopt stops (Ctrl+C, a fault of the program) after some boards joined: they are members, but the steps
    after the JOIN did not run for them."""
    say('NOTE: %d board(s) joined before the stop: %s. They have no friends and no web password of the pool yet: '
        'poolmgr friends --check; poolmgr webpass new <member> (only a board with no web password of its own)' % (
            len(done), ', '.join(m['name'] for m, _ in done)))
    backup_old(data.pool)


def ask_line(prompt):
    """A question with echo (the 6 digits, the number of a board). InputError at the end of the input."""
    try:
        return input(prompt)
    except EOFError:
        raise InputError('the input ended before the answer to a question (do the command in a terminal)')


def find_joins(data, all_):
    """The boards with an open JOIN screen (the 'A' 'J' packets): one board, all (all_), or the choice of the
    person."""
    say('listen %d s for the boards with an open JOIN screen (SETUP > POOL > JOIN)...' % JOIN_LISTEN)
    joins, _ = pm_net.listen(data, JOIN_LISTEN)
    ids = {m['id'] for m in data.pool.members}
    joins = sorted((j for j in joins if j['id'] not in ids), key=lambda j: (j['name'], j['mac']))
    if not joins:
        raise Failed('no board with an open JOIN screen (on the board: SETUP > POOL > JOIN; or give its address)')
    if all_ or len(joins) == 1:
        return joins
    for i, j in enumerate(joins):
        say('  %d  %-16s %s  %s' % (i + 1, j['name'], j['mac'][-4:], j['ip']))
    w = ask_line('the number of the board (or "all"): ').strip().lower()
    if w == 'all':
        return joins
    try:
        return [joins[int(w) - 1]]
    except (ValueError, IndexError):
        raise InputError('no board number %s' % w)


def console_digits(cons, want, timeout=SAS_WAIT):
    """The console that shows the 6 digits of the board: (console, digits). The first SAS line of a console decides:
    other digits = a device between the program and the board. The program sends "pool" one time to each console
    when no line comes in 2 s (the board prints its state again)."""
    end = time.time() + timeout
    asked = time.time() + 2.0
    while time.time() < end:
        got_line = False
        for _, con in cons:
            ln = con.read_line(0.05)
            if ln is None:
                continue
            got_line = True
            mt = pool_tool.SAS_RE.search(ln)
            if mt:
                return con, int(mt.group(1) + mt.group(2))
        if not got_line and asked and time.time() > asked:
            for _, con in cons:
                con.write_line('pool')
            asked = 0
    raise Failed('no console showed the 6 digits in %d s' % timeout)


def kept_webpass(data, t, b):
    """The web password that this pool gave the board before (webpass.json: the board left the pool), or None. Only
    for an address that the person gave (adopt <address>): any device can send a JOIN packet or a GET /api/pool
    answer with the MAC of a board, and the password goes before the check of the device key. The MAC comes from
    GET /api/pool (it needs no password). For a board of a JOIN packet, the line KEPT_HINT names the command."""
    if not t.get('given'):
        mac = mac_of(t.get('mac'))
        if mac and pool_tool.load_webpass(data.pool_dir).get(mac):
            say(KEPT_HINT % (t['ip'], t['ip']))
        return None
    try:
        mac = mac_of(pool_tool.Board(b.ip).call('GET', '/api/pool').get('mac'))
    except (pool_tool.Refused, pool_tool.ToolError):
        return None
    return pool_tool.load_webpass(data.pool_dir).get(mac) if mac else None


def open_board(data, t, password):
    """The first request of the JOIN (GET /api/id), with the web password when the board has one: (the
    pool_tool.Board, the answer of /api/id); b.password is the password that opened the board (None: no password).
    The first try has no password; after a 401 of the web gate the password comes from: password (the option: a
    value = one try; '' = a question), else the password that this pool gave the board before (kept_webpass), else a
    question (a terminal only). The questions give WEB_TRIES tries; an empty answer is a try too."""
    ip = t['ip']
    b = pool_tool.Board(ip)
    kept, tries = None, 0
    while True:
        try:
            return b, b.call('GET', '/api/id')
        except pool_tool.Refused as e:
            if e.code == 429:
                raise Failed('%s refuses web passwords for %s s (too many wrong web passwords): wait, then do the '
                             'adopt again' % (ip, e.ans.get('secs', '?')))
            if e.code != 401:
                raise
        if password:
            if b.password:
                raise Failed(NOT_RIGHT % ip)
            b.password = password
            continue
        if password is None and kept is None:
            kept = kept_webpass(data, t, b) or ''
            if kept:
                say('%s has the web password that this pool gave it before: poolmgr uses it' % ip)
                b.password = kept
                continue
        if tries >= WEB_TRIES:
            raise Failed(NOT_RIGHT % ip)
        if password is None and not can_ask():
            raise Failed(WEB_HINT % ip)
        tries += 1
        if tries > 1:
            prompt = 'Not right. The web password of the board %s again: ' % ip
        elif password is None:
            prompt = '%s has a web password. The web password of the board (SETUP > WEB PASSWORD): ' % ip
        else:
            prompt = 'The web password of the board %s (SETUP > WEB PASSWORD): ' % ip
        b.password = ask_secret(prompt)


def join_one(data, t, cons, digits, name, role, password=None):
    """The JOIN of one board (pool_tool.py adopt, design 2.7.1). Gives (the new member dict, own): own = the board
    keeps its own web password (the password that opened it is not the one of webpass.json). An entry of webpass.json
    that does not open the board goes out after the JOIN ("webpass show" must not print a password that does not
    work)."""
    ip = t['ip']
    key_a(data, data.pool)
    b, ident = open_board(data, t, password)
    pin = ident_key(ip, ident)
    if t.get('id') and t['id'] != pool_tool.ident_id(ident):
        raise Failed('the board at %s is not the board of the JOIN packet (ID %s)' % (ip, t['id']))
    j = pool.Join(data.key_a)
    try:
        f = j.take_hello(b.call('POST', '/api/pool?op=hello', j.hello_body()), pin)
    except pool.PoolError as e:
        raise Failed('the op=hello answer of %s is not good: %s' % (ip, e))
    say('board %s: device ID %s, its JOIN signature and box key verify' % (ip, f['id'].hex()))
    b.call('POST', '/api/pool?op=reveal', j.reveal_body())
    want = j.sas()
    say('the 6 digits: %s' % pool.sas_text(want))
    if cons:
        con, got = console_digits(cons, want)
    elif digits:
        con, got = None, int(re.sub(r'\D', '', digits) or -1)
    else:
        con, got = None, int(re.sub(r'\D', '', ask_line('the 6 digits on the screen of the board: ')) or -1)
    if got != want:
        raise Failed('the board shows %s, the program %s: NOT the same (a device between them?)' % (
            pool.sas_text(got) if got >= 0 else '?', pool.sas_text(want)))
    if con is not None:
        con.write_line('pool allow %06d' % want)
    else:
        say('the digits are the same: tap ALLOW on the board (%d s)' % ALLOW_WAIT)
    did = f['id'].hex()
    mac = pool_tool.ident_mac(ident, b)
    with data.change():
        p = load_locked(data)
        key = key_a(data, p)
        try:
            m = p.upsert({'id': did, 'pk': f['pk'].hex(), 'mac': mac, 'role': pool.ROLES.index(role), 'flags': 0,
                          'name': name or 'TFN-%s' % did[:4].upper(), 'bx': f['bx'].hex(), 'ip': ip})
        except pool.PoolError as e:
            raise InputError(str(e))
        roster = p.sign(key)
        set_wait(b, j.set_body(roster, p.epoch, p.pool_k, p.wk(did)))
        p.save(data.pool_dir)
        data.pool = p
        data.set_addr(mac, ip, '', did)
        out_drop(data, mac)
        own = stale_webpass(data, m, b.password)
        data.log('adopt %s: %s (%s), roster version %d' % (m['name'], did, mac, p.version))
        joined_check(ip, m, p)
        say('%s is a member of the pool %s (roster version %d)' % (m['name'], p.name, p.version))
        others = [o for o in pm_board.boards(data) if o['id'] != did]
        if others:
            say('the new roster to the other members:')
            send_all(data, others, with_key=False)
    return m, own


def joined_check(ip, m, p):
    """The check after the JOIN: GET /api/pool of the board gives the pool and the roster version of p. The board is
    in the roster now, so an answer that does not come (or is not of a board) gives only a WARN line: the steps after
    the JOIN (the web password, the friends) still run. A board that answers with another state stops (Failed)."""
    try:
        st = pool_tool.Board(ip).call('GET', '/api/pool')
    except (pool_tool.Refused, pool_tool.ToolError) as e:
        say('WARN: %s is in the roster of the pool %s, but the check of its state did not answer (%s): poolmgr list '
            '--live' % (m['name'], p.name, why(e)))
        return
    if st.get('state') != 'ok' or st.get('pool') != p.pool_id.hex() or st.get('v') != p.version:
        raise Failed('%s took the roster, but its state is %s (pool %s, version %s)' % (
            ip, st.get('state'), st.get('pool'), st.get('v')))


def ident_key(ip, ident):
    """The device key of an answer of /api/id (65 bytes; its device ID is the "id" of the answer). Failed for an
    answer that is not a JSON object, or a "pk" that is not hex text of a key of that ID: with adopt --all, a device
    with a bad answer stops only its own JOIN."""
    try:
        pin = bytes.fromhex(ident.get('pk', ''))
        good = len(pin) == 65 and pool_crypto.device_id(pin).hex() == pool_tool.ident_id(ident)
    except (AttributeError, TypeError, ValueError):
        good = False
    if not good:
        raise Failed('/api/id of %s gives no good device key' % ip)
    return pin


def stale_webpass(data, m, password):
    """After the JOIN, in the change lock: own (the board has a web password that is not the one of webpass.json: it
    keeps it). An entry of webpass.json that is not the password that opened the board (the board has no web
    password, or another one) goes out: "webpass show" must not print a password that does not open the board."""
    wp = pool_tool.load_webpass(data.pool_dir)
    old = wp.get(m['mac'])
    if old is not None and old != password:
        wp.pop(m['mac'])
        save_webpass(data, wp)
        say('%s: the web password that this pool kept for it does not open it now: it goes out of the data folder' %
            m['name'])
    return bool(password) and password != old


def set_wait(b, body):
    """op=set of the JOIN: the board answers "wait" (409) until a person taps ALLOW. Failed when nobody taps ALLOW in
    ALLOW_WAIT s, or when the board ends the JOIN (NO, or its 2 minutes: then op=set needs the request header)."""
    end = time.time() + ALLOW_WAIT
    while True:
        try:
            return b.call('POST', '/api/pool?op=set', body)
        except pool_tool.Refused as e:
            code = e.ans.get('error')
            if e.code == 409 and code == 'wait':
                if time.time() > end:
                    raise Failed(NO_TAP % ALLOW_WAIT)
                time.sleep(0.5)
                continue
            if e.code == 401 and code != 'login':
                raise Failed(ENDED)
            raise


def after_join(data, m, webpass_, friends_, own=False):
    """The defaults after the JOIN (design 2.6 step 5): the web password, the friends, the store mode."""
    if webpass_ and own:
        say('%s keeps its own web password (SETUP > WEB PASSWORD on the board); poolmgr webpass new %s gives it a '
            'random one' % (m['name'], m['name']))
    elif webpass_:
        try:
            webpass(data, 'new', [m['id']])
        except Failed as e:
            say('%s: %s' % (m['name'], e))
    if friends_:
        try:
            make_friends(data, False, m)
        except Failed as e:
            say('%s: %s' % (m['name'], e))
    if data.cfg.get('store') == 'local':
        try:
            for ln in pm_store.set_mode(data, 'local') or ():
                say(ln if isinstance(ln, str) else '  '.join('%s=%s' % (k, ln[k]) for k in ln))
        except (InputError, Failed) as e:
            say('%s: the local store is not set: %s (poolmgr store local)' % (m['name'], e))


def rekey(p):
    """A new pool key (the next epoch). The previous key keeps the time of the change (secrets.json "prev" "t"):
    the listener opens the beacons of the previous key for PREV_KEEP s after it (pm_net.pool_keys)."""
    p.new_key()
    p.prev[0]['t'] = int(time.time())


# ---------------------------------------------------------------- the roster and the key to the members
def send_one(data, m, with_key=True):
    """The newest roster (and the key: op=key) to one member: the answer of the board."""
    p = data.pool
    if with_key:
        body = pool.key_body(bytes.fromhex(m['bx']), bytes.fromhex(m['id']), p.roster_file, p.epoch, p.pool_k,
                             p.wk(m['id']))
        return pm_board.call(data, m, 'POST', '/api/pool?op=key', body)
    return pm_board.call(data, m, 'POST', '/api/pool?op=set', {'roster': p.roster_file.hex()})


def send_all(data, members, with_key=True):
    """send_one to each member: the number of members that did not take it."""
    bad = 0
    for m in members:
        try:
            ans = send_one(data, m, with_key)
            say('  %s: %s' % (m['name'], ans.get('state', 'ok')))
        except (pool_tool.Refused, pool_tool.ToolError, Failed) as e:
            bad += 1
            say('  %s: FAILED: %s' % (m['name'], why(e)))
            data.log('send to %s: %s' % (m['name'], why(e)))
    return bad


def push(data, members):
    """The newest roster and key to the members (all when members is empty), then the friend entries that wait for
    their removal."""
    with data.change():
        p = load_locked(data)
        key = key_a(data, p)
        if not p.roster_file or pool.roster_parse(p.roster_file).version != p.version:
            p.sign(key)
            p.save(data.pool_dir)
        ms = [pm_board.find_member(data, w) for w in members] if members else pm_board.boards(data)
        say('roster version %d, epoch %d to %d member(s):' % (p.version, p.epoch, len(ms)))
        bad = send_all(data, ms)
        for ln in out_retry(data):
            say('  ' + ln)
    if bad:
        raise Failed('%d of %d members did not take the roster and the key' % (bad, len(ms)))
    return ['PUSH OK: %d member(s)' % len(ms)]


# ---------------------------------------------------------------- remove (design 2.8)
def mgr_clear(data, m):
    """op=mgr with empty values: the board has no manager store (AUTO = the official static store). The store module
    sends it (pm_store.set_member: it adds the store token of the board, which a board with a token needs)."""
    return pm_store.set_member(data, m, 'official')


class NotHere(Failed):
    """Another board, or a device that is not a TFN board, answers at the address of a member (an old address: the
    LAN gave it to another device)."""


def check_id(ip, m):
    """GET /api/pool at ip (it needs no password, so no password goes out) must give the device ID of the member m:
    the answer, else NotHere (another board, or a device that is not a TFN board: for example the web page of a
    router)."""
    try:
        st = pool_tool.Board(ip).call('GET', '/api/pool')
    except pool_tool.NotBoard:
        raise NotHere('another device answers at %s, an old address of %s (it is not a TFN board)' % (ip, m['name']))
    if str(st.get('id') or '').lower() != m['id']:
        raise NotHere('another board answers at %s, an old address of %s' % (ip, m['name']))
    return st


def member_board(data, m):
    """The pool_tool.Board of the member m (its address, its wk and its web password) after check_id() at its
    address: the requests of remove (and the web password with them) go only to the board of the member. A member
    that does not answer at its last address: one listen for its beacon, then the check at the new address
    (pm_board.run). Failed (no address), NotHere, pool_tool.Refused or ToolError (no answer)."""
    seen = []

    def probe(b):
        seen.append(b)
        return check_id(b.ip, m)
    pm_board.run(data, m, probe)
    return seen[-1]


def away(e):
    """The words for a member that gets no request in remove (the fault of member_board)."""
    if isinstance(e, NotHere):
        return 'is not at its address (%s)' % e
    return 'did not answer (%s)' % why(e)


def remove(data, member, keep_webpass=False):
    """Remove the member in the order of design 2.8: (1) the official store, (2) the friends out, (3) the web
    password of the pool off (keep_webpass: the board keeps it), (4) a new roster with no member and a new pool key
    (op=set to the member: it leaves; op=key to the others). The requests go to the member only when the board at its
    address has its device ID (member_board). A member that does not answer, or another board or device at its
    address: no request goes to it, and it still leaves: the new key cuts it off; its web password stays in
    webpass.json ("webpass show <MAC>")."""
    with data.change():
        p = load_locked(data)
        key = key_a(data, p)
        m = pm_board.find_member(data, member)
        pool_tool.load_webpass(data.pool_dir)   # (a damaged file stops the command here, before a change)
        pm_store.load_tokens(data)
        out_load(data)
        say('remove %s (%s):' % (m['name'], mac_text(m)))
        try:
            gone, absent = member_board(data, m), None   # (its wk and address before the change)
        except (pool_tool.Refused, pool_tool.ToolError, Failed) as e:
            gone, absent = None, e
            say('  %s %s: no request goes to it' % (m['name'], away(e)))
        store_off(data, m, absent)
        others = [o for o in pm_board.boards(data) if o['id'] != m['id']]
        say('  (2) the friends out:')
        friends_out(data, m, others, absent)
        web_off(data, m, gone, keep_webpass, absent)
        p.remove(m['id'])
        rekey(p)
        roster = p.sign(key)
        p.save(data.pool_dir)
        data.log('remove %s (%s): roster version %d, epoch %d' % (m['name'], m['id'], p.version, p.epoch))
        leave(m, gone, absent, roster)
        say('  (4) the new key (epoch %d) to the other members:' % p.epoch)
        bad = send_all(data, others)
    backup_old(p)
    if bad:
        raise Failed('%d member(s) did not get the new key: poolmgr push (serve does it by itself)' % bad)
    return ['%s is out of the pool %s (roster version %d, epoch %d)' % (m['name'], p.name, p.version, p.epoch)]


def store_off(data, m, absent):
    """Step (1) of remove: the official store on the member (no request when absent)."""
    if absent is not None:
        say('  (1) %s: no store change' % m['name'])
        return
    try:
        mgr_clear(data, m)
        say('  (1) %s: the official store' % m['name'])
    except (pool_tool.Refused, pool_tool.ToolError, Failed, ValueError, KeyError) as e:
        say('  (1) %s: no store change: %s' % (m['name'], why(e)))


def leave(m, gone, absent, roster):
    """Step (4) of remove: op=set with the new roster to the member (it leaves the pool); no request when absent."""
    cut = 'the new key cuts it off; it shows its pool until a person taps LEAVE on it'
    if absent is not None:
        say('  (4) %s %s: %s' % (m['name'], away(absent), cut))
        return
    try:
        ans = pm_board.signed(gone, 'POST', '/api/pool?op=set', {'roster': roster.hex()})
        say('  (4) %s left the pool: %s' % (m['name'], ans.get('state', 'ok')))
    except (pool_tool.Refused, pool_tool.ToolError) as e:
        say('  (4) %s %s: %s' % (m['name'], away(e), cut))


def web_off(data, m, gone, keep, absent=None):
    """Step (3) of remove: the web password that the pool gave the member (webpass.json). Off on the board, and out of
    webpass.json only when the board answers with no password after the change (the board answers op=webpass
    before it changes the password). keep: the board keeps it. The board gets no request when absent, or when
    check_id() fails again just before the first request with the password: then it keeps the password. A kept
    password stays in webpass.json: "webpass show <MAC>" prints it. A board with another web password now (a person
    set it on the board) keeps that password."""
    wp = pool_tool.load_webpass(data.pool_dir)
    pw = wp.get(m['mac'])
    if not pw:
        say('  (3) %s has no web password of the pool (its own web password stays as it is)' % m['name'])
        return
    show = 'poolmgr webpass show %s prints it' % mac_text(m)
    if keep:
        say('  (3) %s keeps the web password of the pool: %s' % (m['name'], show))
        return
    kept = 'it keeps the web password of the pool: %s; or change it on the board: SETUP > WEB PASSWORD' % show
    if gone is None:
        say('  (3) %s %s: %s' % (m['name'], away(absent or Failed('no address')), kept))
        return
    try:
        check_id(gone.ip, m)
        now = web_now(gone.ip, pw)
        if now == 'pool':
            pm_board.signed(gone, 'POST', '/api/pool?op=webpass', {'pw': ''})
            if web_now(gone.ip, pw, tries=3) != 'none':
                say('  (3) %s did not turn off the web password (the board still asks for a password): %s' % (
                    m['name'], kept))
                return
    except (pool_tool.Refused, pool_tool.ToolError, Failed) as e:
        say('  (3) %s %s: %s' % (m['name'], away(e), kept))
        return
    wp.pop(m['mac'], None)
    save_webpass(data, wp)
    if now == 'other':
        say('  (3) %s has another web password now (not the one of the pool): it keeps it' % m['name'])
    else:
        say('  (3) %s has no web password now (the web password of the pool is off). Set your own on the board: '
            'SETUP > WEB PASSWORD' % m['name'])


def web_now(ip, pw, tries=1):
    """The web password of the board at ip now: 'none', 'pool' (the password pw) or 'other' (a person set another
    one). Two requests with no request header: GET /api/id with no password, then with pw. tries: the tries when the
    board does not answer (after op=webpass the board ends its open connections)."""
    for i in range(tries):
        try:
            return web_state(ip, pw)
        except pool_tool.ToolError:
            if i == tries - 1:
                raise
            time.sleep(0.5)


def web_state(ip, pw):
    """One try of web_now()."""
    for password, name in ((None, 'none'), (pw, 'pool')):
        try:
            pool_tool.Board(ip, password=password).call('GET', '/api/id')
            return name
        except pool_tool.Refused as e:
            if e.code != 401:
                raise
    return 'other'


# ---------------------------------------------------------------- the friends (design 2.7)
def friend_call(data, m, query):
    return pm_board.call(data, m, 'GET', '/api/friends?' + urllib.parse.urlencode(query))


def friend_add(data, m, o):
    """o becomes a friend of m: 'ok', 'full' or the fault."""
    try:
        ans = friend_call(data, m, [('add', mac_text(o)), ('name', o['name'])])
    except (pool_tool.Refused, pool_tool.ToolError, Failed) as e:
        return why(e)
    return ans.get('add', 'ok')


def friend_macs(data, m):
    ans = friend_call(data, m, [])
    return {str(f.get('mac', '')).upper().replace(':', '') for f in ans.get('friends') or ()}


def friends_out(data, m, others, absent=None):
    """The MAC of m out of the friends of each other member, and the others out of the friends of m (absent: the
    fault of member_board; then no request goes to m). A member that does not take it gets a line, and the entry
    waits in friends_out.json for the next try (push, friends, serve)."""
    for o in others:
        for a, b in ((o, m), (m, o)):
            later = a is o
            e = None if later else absent
            if e is None:
                try:
                    friend_call(data, a, [('rm', mac_text(b))])
                    say('    %s: %s out' % (a['name'], b['name']))
                    continue
                except (pool_tool.Refused, pool_tool.ToolError, Failed) as x:
                    e = x
            say('    %s: %s not out: %s%s' % (a['name'], b['name'], why(e), (
                ' (poolmgr tries again: push, friends, serve)' if later else ' (on %s: SETUP > FRIENDS)' % m['name'])))
            if later:
                out_add(data, o, m)


def out_load(data):
    """The friend entries that wait for their removal: [{"on": the MAC of the member, "mac": the MAC to take out,
    "name": the name of the board that left}]. InputError for a damaged file: the program stops with its name, and it
    does not write over the entries that wait."""
    path = os.path.join(data.pool_dir, OUT_FILE)
    form = '{"format": 1, "pending": [...]}'
    j = pm_data.read_strict(path, {}, dict, form)
    items = j.get('pending', [])
    if not isinstance(items, list):
        raise pm_data.damaged(path, 'not the form %s' % form)
    return [x for x in items if isinstance(x, dict) and x.get('on') and x.get('mac')]


def out_save(data, items):
    pm_data.write_file(os.path.join(data.pool_dir, OUT_FILE),
                       (json.dumps({'format': 1, 'pending': items}, indent=1, sort_keys=True) + '\n').encode('utf-8'))


def out_add(data, on, gone):
    """The member on keeps the friend entry of gone (a board out of the pool): it waits for the next try."""
    items = out_load(data)
    if not any((x['on'], x['mac']) == (mac_text(on), mac_text(gone)) for x in items):
        items.append({'on': mac_text(on), 'mac': mac_text(gone), 'name': gone['name'], 't': int(time.time())})
        out_save(data, items)


def out_drop(data, mac):
    """A board that joins the pool again: its friend entries stay (no removal waits for its MAC)."""
    items = out_load(data)
    keep = [x for x in items if x['mac'] != mac.upper()]
    if keep != items:
        out_save(data, keep)


def out_waits(data, mac):
    """True when the member with the MAC has a friend entry that waits for its removal (serve)."""
    return any(x['on'] == mac.upper() for x in out_load(data))


def out_retry(data, only=None):
    """The friend entries that wait for their removal: ?rm= again (only: the entries of one member). The lines of
    the result. An entry of a board that is a member again, or of a member that is not in the pool now, goes."""
    items = out_load(data)
    if not items:
        return []
    macs = {mac_text(m): m for m in pm_board.boards(data)}
    keep, lines = [], []
    for x in items:
        on = macs.get(x['on'])
        if x['mac'] in macs or on is None:
            continue
        if only is not None and on['id'] != only['id']:
            keep.append(x)
            continue
        gone = x.get('name') or x['mac']
        try:
            friend_call(data, on, [('rm', x['mac'])])
            lines.append('%s: %s out of its friends (a friend entry of an earlier remove)' % (on['name'], gone))
        except (pool_tool.Refused, pool_tool.ToolError, Failed) as e:
            keep.append(x)
            lines.append('%s: %s is not out of its friends yet: %s' % (on['name'], gone, why(e)))
    if keep != items:
        out_save(data, keep)
    return lines


def friends(data, check=False):
    """Each member a friend of each other member (design 2.7); check: read the list of each member and add only the
    missing entries (a board that was off line at the adopt). Both do the friend removals that wait."""
    return make_friends(data, check, None)


def make_friends(data, check, only):
    """The work of friends(); only: the pairs of this member only (the adopt). A friend entry is one direction (m
    has o): 3 members have 6 entries. Failed when an entry is missing at the end (a member did not answer, or a full
    friend list: the board refused it)."""
    pm_board.the_pool(data)
    with data.change():
        for ln in out_retry(data):
            say('  ' + ln)
    ms = pm_board.boards(data)
    have = {}
    bad, new, known, full = 0, 0, 0, []
    for m in ms:
        try:
            have[m['id']] = friend_macs(data, m)
        except (pool_tool.Refused, pool_tool.ToolError, Failed) as e:
            if check:
                bad += 1
                say('  %s: no friend list: %s' % (m['name'], why(e)))
    for m in ms:
        if check and m['id'] not in have:
            continue
        for o in ms:
            if o['id'] == m['id'] or (only is not None and only['id'] not in (m['id'], o['id'])):
                continue
            there = mac_text(o) in have.get(m['id'], ())
            if check and there:
                continue
            r = friend_add(data, m, o)
            if r == 'ok':
                known, new = (known + 1, new) if there else (known, new + 1)
            elif r == 'full':
                full.append(m['name'])
                say('FRIENDS FULL ON %s (%d friends): %s is not its friend' % (m['name'], FRIENDS_MAX, o['name']))
            else:
                bad += 1
                say('  %s: %s is not its friend: %s' % (m['name'], o['name'], r))
    say('friends: %d new friend entries, %d were there (one entry is one direction: 3 boards have 6)' % (new, known))
    if bad or full:
        why_ = []
        if full:
            why_.append('FULL ON %s (remove a friend there: SETUP > FRIENDS; then poolmgr friends --check)' %
                        ', '.join(sorted(set(full))))
        if bad:
            why_.append('%d member(s) or entries did not answer (poolmgr friends --check later)' % bad)
        raise Failed('friend entries are missing: %s' % '; '.join(why_))
    return ['FRIENDS OK: %d new friend entries' % new]


# ---------------------------------------------------------------- the web password (design 2.9)
def webpass(data, op, members):
    """op 'show' (one member, or the MAC of a board that left the pool with the web password of the pool) or 'new'
    (a new random web password for the members; all when members is empty). 'new' keeps a new password only when the
    board takes it (webpass_set): else the entry of webpass.json stays as it was, and the member counts as failed. A
    board that takes op=webpass but does not answer the read after it: the new password stays (NOT CHECKED), and the
    member counts as failed."""
    pm_board.the_pool(data)
    if op == 'show':
        if len(members) != 1:
            raise InputError('webpass show takes one member')
        wp = pool_tool.load_webpass(data.pool_dir)
        try:
            m = pm_board.find_member(data, members[0])
        except InputError:
            mac = mac_of(members[0])
            if mac and wp.get(mac):
                return ['%s (not a member of the pool now): user admin, web password %s' % (mac.upper(), wp[mac])]
            raise
        pw = wp.get(m['mac'])
        if not pw:
            raise Failed('%s has no web password of poolmgr (poolmgr webpass new %s)' % (m['name'], m['name']))
        return ['%s: user admin, web password %s' % (m['name'], pw)]
    if op != 'new':
        raise InputError('webpass show <member> | new [<member>...]')
    with data.change():
        load_locked(data)
        ms = [pm_board.find_member(data, w) for w in members] if members else pm_board.boards(data)
        bad = 0
        for m in ms:
            pw = secrets.token_urlsafe(12)
            try:
                now = webpass_set(data, m, pw)
            except (pool_tool.Refused, pool_tool.ToolError, Failed) as e:
                bad += 1
                say('  %s: FAILED: %s' % (m['name'], why(e)))
                continue
            if now in WEB_NOW:
                bad += 1
                say('  %s: FAILED: the board answered, but the new web password does not open it (%s): the data '
                    'folder keeps the web password that it had before' % (m['name'], WEB_NOW[now]))
                data.log('webpass new %s: the board did not take it (%s)' % (m['name'], now))
                continue
            wp = pool_tool.load_webpass(data.pool_dir)
            wp[m['mac']] = pw
            save_webpass(data, wp)
            data.log('webpass new %s%s' % (m['name'], '' if now == 'pool' else ': not checked'))
            if now == 'pool':
                say('  %s: a new web password (poolmgr webpass show %s)' % (m['name'], m['name']))
                continue
            bad += 1
            say('  %s: NOT CHECKED: the board took the new web password, but it did not answer the check after it '
                '(%s). The data folder keeps the new password (poolmgr webpass show %s); when it does not open the '
                'board, do poolmgr webpass new %s again' % (m['name'], now, m['name'], m['name']))
    if bad:
        raise Failed('%d of %d members have no new web password that the program checked' % (bad, len(ms)))
    return ['WEBPASS OK: %d member(s)' % len(ms)]


def webpass_set(data, m, pw):
    """op=webpass with the new web password pw to the member, then a read of the board at the same address (the board
    answers op=webpass before it sets the password): web_now() of pw, 'pool' when the board takes it. When the board
    takes op=webpass but does not answer the read, the text of the fault: the board can have the new password, so the
    caller keeps it."""
    def send(b):
        pm_board.signed(b, 'POST', '/api/pool?op=webpass', {'pw': pw})
        return b.ip
    ip = pm_board.run(data, m, send)
    try:
        return web_now(ip, pw, tries=3)
    except (pool_tool.Refused, pool_tool.ToolError) as e:
        return why(e)


# ---------------------------------------------------------------- list
def pool_state(b):
    """GET /api/pool at the address of the pool_tool.Board b, with no web password (the request needs none)."""
    return pool_tool.Board(b.ip).call('GET', '/api/pool')


def store_of(data, m):
    """The store of a member from GET /api/apps ("mgr": the board has a manager store): 'local', 'official' or
    '?'."""
    try:
        apps = pm_board.call(data, m, 'GET', '/api/apps')
    except (pool_tool.Refused, pool_tool.ToolError, Failed, ValueError):
        return '?'
    return 'local' if apps.get('mgr') else 'official'


def list_members(data, live=False):
    """The members as lines of text; live: also the state of each board (design 2.4 "list")."""
    p = pm_board.the_pool(data)
    mode = data.cfg.get('store', 'official')
    out = ['pool %s  ID %s  roster version %d  epoch %d  store %s  admin keys %s' % (
        p.name, p.pool_id.hex(), p.version, p.epoch, mode, ' '.join(pool_crypto.key_id(k).hex() for k in p.admins))]
    if live:
        pool_tool.load_webpass(data.pool_dir)   # (a damaged file stops the command here, not each board)
    bad = 0
    for m in pm_board.boards(data):
        ln = '%-16s %s  %s  %-6s %s' % (m['name'], m['id'], mac_text(m), pool.ROLES[m['role']],
                                         pm_board.known_address(data, m) or '-')
        if live:
            try:
                st = pm_board.run(data, m, pool_state)
                me = pm_board.run(data, m, lambda b: pm_board.signed(b, 'GET', '/api/boards')).get('me') or {}
                same = st.get('pool') == p.pool_id.hex() and st.get('v') == p.version and st.get('epoch') == p.epoch
                v4 = st.get('v4') or {}
                store = store_of(data, m)
                ln += '  [%s v%s e%s%s] fw %s kind %s sig %s own_sig %s minsv %s app %s v4 ok %s bad %s store %s%s' % (
                    st.get('state'), st.get('v'), st.get('epoch'), '' if same else ' NOT THE NEWEST',
                    me.get('ver', '?'), me.get('kind', me.get('board', '?')), me.get('sig', '?'),
                    me.get('own_sig', '?'), me.get('minsv', '?'), me.get('app') or 'MENU', v4.get('ok', '?'),
                    v4.get('bad', '?'), store, '' if store in ('?', mode) else
                    ' (NOT THE STORE OF THE POOL: poolmgr store %s)' % mode)
                bad += 0 if same and st.get('state') == 'ok' else 1
            except (pool_tool.Refused, pool_tool.ToolError, Failed) as e:
                ln += '  [no answer: %s]' % why(e)
                bad += 1
        out.append(ln)
    if live and bad:
        for ln in out:
            say(ln)
        raise Failed('%d member(s) do not answer or do not have the newest roster and key (poolmgr push)' % bad)
    return out


# ---------------------------------------------------------------- the upkeep (serve, design 2.8)
def upkeep(data, mac, st):
    """The upkeep of one member for serve (design 2.8): st = the state of GET /api/pool of the member, or its
    beacon (then the function reads GET /api/pool). The friend entries that wait for their removal on the member go
    out; a member with an older roster or key, or "damaged", gets the newest (at most once in UPKEEP_S); a member that
    says "none" with its own ID (LEAVE on its screen) is removed with a new key for the others. Gives the lines of what
    it did ([] = nothing)."""
    data.load()
    p = data.pool
    if p is None:
        return []
    m = p.find(mac.lower().replace(':', ''))
    if m is None or pm_board.is_manager(m):
        return []
    lines = []
    if out_waits(data, mac_text(m)):
        with data.change():
            lines += out_retry(data, m)
    if 'state' not in (st or {}):
        try:
            st = pm_board.run(data, m, pool_state, wait=0)
        except (pool_tool.Refused, pool_tool.ToolError, Failed):
            return lines
    now = time.time()
    state = st.get('state')
    if state == 'none' and str(st.get('id') or '').lower() == m['id']:
        if now - _fix_t.get('rm' + m['id'], 0) < UPKEEP_S:
            return lines
        _fix_t['rm' + m['id']] = now
        try:
            return lines + ['%s left the pool on its screen (LEAVE): it goes out of the roster' % m['name']] + \
                remove_left(data, m)
        except (Failed, InputError) as e:
            return lines + ['%s left the pool on its screen (LEAVE): %s' % (m['name'], e)]
    same = st.get('pool') == p.pool_id.hex()
    old = state == 'ok' and same and (st.get('v', 0) < p.version or st.get('epoch', 0) < p.epoch)
    if not (old or state == 'damaged') or now - _fix_t.get(m['id'], 0) < UPKEEP_S:
        return lines
    _fix_t[m['id']] = now
    try:
        with data.change():
            p = load_locked(data)
            key_a(data, p)
            m = p.find(m['id'])
            if m is None:
                return lines
            ans = send_one(data, m)
        data.log('upkeep %s: the newest roster and key (it had version %s, epoch %s, state %s)' % (
            m['name'], st.get('v'), st.get('epoch'), state))
        return lines + ['upkeep %s: roster version %d, epoch %d: %s' % (m['name'], p.version, p.epoch,
                                                                        ans.get('state', 'ok'))]
    except (pool_tool.Refused, pool_tool.ToolError, Failed, InputError) as e:
        data.log('upkeep %s: %s' % (m['name'], why(e)))
        return lines + ['upkeep %s: FAILED: %s' % (m['name'], why(e))]


def remove_left(data, m):
    """A member that left (LEAVE on its screen): a roster without it and a new pool key for the others (no request
    to the member: it is in no pool now), the friends of the pool out (a member that does not take it: a line, and the
    entry waits in friends_out.json). Its web password stays in webpass.json ("webpass show <MAC>")."""
    lines = []
    with data.change():
        p = load_locked(data)
        key = key_a(data, p)
        if p.find(m['id']) is None:
            return []
        others = [o for o in pm_board.boards(data) if o['id'] != m['id']]
        for o in others:
            try:
                friend_call(data, o, [('rm', mac_text(m))])
            except (pool_tool.Refused, pool_tool.ToolError, Failed) as e:
                out_add(data, o, m)
                lines.append('%s: %s is not out of its friends: %s (the next upkeep tries again)' % (
                    o['name'], m['name'], why(e)))
        p.remove(m['id'])
        rekey(p)
        p.sign(key)
        p.save(data.pool_dir)
        data.log('the upkeep removed %s (LEAVE on the board): roster version %d, epoch %d' % (
            m['name'], p.version, p.epoch))
        bad = send_all(data, others)
        has_pw = bool(pool_tool.load_webpass(data.pool_dir).get(m['mac']))
    out = ['removed; roster version %d, epoch %d to %d member(s)%s' % (
        p.version, p.epoch, len(others) - bad, ', %d did not answer' % bad if bad else '')] + lines
    if has_pw:
        out.append('%s keeps the web password of the pool: poolmgr webpass show %s prints it; or change it on the '
                   'board: SETUP > WEB PASSWORD' % (m['name'], mac_text(m)))
    return out + ['BACKUP IS OLD: poolmgr backup %s' % backup_name(p)]
