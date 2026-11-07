#!/usr/bin/env python3
"""The test of sdk/tools/release_cfg.py: the public keys and the OTA mode of a public release build
(.github/workflows/release.yml).

  - header: the header of the keys, and its checks of each key value;
  - force: the force_cfg values of boards.json and the OTA mode (ENFORCE or REPORT) in a sdkconfig, also with the
    marks "# default:" of ESP-IDF 6 and CRLF line ends; a second run changes nothing;
  - check: the OTA mode in the sdkconfig.h of a build;
  - release_cfg: the release_cfg values of boards.json (force writes them, also into a file that does not have them;
    check --boards finds a lost one; an OTA option in release_cfg stops the tool); the release_cfg of the real
    boards.json goes through force;
  - keys: the image must hold the 65 bytes of each key (a wrong key list stops the job);
  - the C check: main/tfn_defaults.h reads the header, and the key check of main/ota.c (its own lines) passes in
    ENFORCE with the keys and stops at its #error with no key. It needs a C compiler (CC, else gcc, clang or cc); with
    none it says SKIP.

Usage: python tests/release/test_release_cfg.py   (TEST key values only: the test makes no key)
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'sdk', 'tools'))
import release_cfg as R  # noqa: E402

FAILS = []
KEY_A = '04' + 'a1' * 64
KEY_B = '04' + 'b2' * 64
KEY_S = '04' + 'c3' * 64


def expect(cond, what):
    print(('ok   ' if cond else 'FAIL ') + what)
    if not cond:
        FAILS.append(what)


def write(path, text):
    with open(path, 'wb') as f:
        f.write(text.encode())


def read(path):
    with open(path, 'rb') as f:
        return f.read().decode()


def lines_of(path):
    return [x.rstrip('\r') for x in read(path).split('\n')]


def test_header(tmp):
    out = os.path.join(tmp, 'keys.h')
    rc = R.main(['header', '--out', out, '--store-pub', KEY_S, '--ota-pub', KEY_A.upper()])
    text = read(out)
    expect(rc == 0 and text.count('#define ') == 2 and 'TFN_OTA_PUBKEY2_BYTES' not in text, 'header: 2 keys')
    expect('#define TFN_OTA_PUBKEY1_BYTES 0x04, 0xa1, 0xa1' in text, 'header: the OTA key as C bytes')
    expect(text.count('#ifndef ') == 2, 'header: each value only when it is not defined')
    rc = R.main(['header', '--out', out, '--store-pub', KEY_S, '--ota-pub', KEY_A, '--ota-pub2', KEY_B])
    expect(rc == 0 and '#define TFN_OTA_PUBKEY2_BYTES 0x04, 0xb2' in read(out), 'header: the second OTA key')
    for bad, what in ((KEY_A[:-2], 'a short key'), ('05' + KEY_A[2:], 'not 04'), ('', 'no key'),
                      (KEY_A[:-1] + 'g', 'not hex')):
        expect(R.main(['header', '--out', out, '--store-pub', KEY_S, '--ota-pub', bad]) == 1,
               'header: stops for %s' % what)


def test_force(tmp):
    boards = os.path.join(tmp, 'boards.json')
    write(boards, '{"force_cfg": {"_note": "x", "CONFIG_LWIP_MAX_SOCKETS": "16", "CONFIG_MBEDTLS_CHACHA20_C": "y",'
                  ' "CONFIG_LCD_DSI_ISR_CACHE_SAFE": "y"}}')
    cfg = os.path.join(tmp, 'sdkconfig')
    start = ('# default:\nCONFIG_LWIP_MAX_SOCKETS=10\n# CONFIG_MBEDTLS_CHACHA20_C is not set\n# default:\n'
             'CONFIG_TFN_OTA_SIG_REPORT=y\n# default:\n# CONFIG_TFN_OTA_SIG_ENFORCE is not set\n# default:\n'
             '# CONFIG_TFN_OTA_SIG_OFF is not set\n# default:\n# CONFIG_TFN_OTA_CRASH_TEST is not set\n')
    for mode, crlf in (('enforce', False), ('report', False), ('enforce', True)):
        text = start.replace('\n', '\r\n') if crlf else start
        write(cfg, text)
        rc = R.main(['force', cfg, '--boards', boards, '--ota-mode', mode])
        got = read(cfg)
        lines = lines_of(cfg)
        name = 'force %s%s' % (mode, ' CRLF' if crlf else '')
        on = R.MODES[mode]
        want = [R.line_of(k, 'y' if k == on else 'n') for k in R.OTA] + ['CONFIG_LWIP_MAX_SOCKETS=16',
                                                                         'CONFIG_MBEDTLS_CHACHA20_C=y']
        expect(rc == 0 and all(lines.count(w) == 1 for w in want), name + ': the values')
        expect('CONFIG_LCD_DSI_ISR_CACHE_SAFE=y' not in lines, name + ': no board option that the file does not have')
        marks = [i for i, x in enumerate(lines) if x == '# default:']
        expect(len(marks) == 1 and lines[marks[0] + 1] == '# CONFIG_TFN_OTA_CRASH_TEST is not set',
               name + ': no "# default:" above a forced option (the other marks stay)')
        expect(('\r\n' in got) == crlf and (crlf is False or '\n' not in got.replace('\r\n', '')),
               name + ': the line ends stay')
        expect(R.force(cfg, dict(R.board_values(boards), **R.ota_values(mode)), R.OTA) == 0,
               name + ': a second run changes nothing')
    write(cfg, 'CONFIG_LWIP_MAX_SOCKETS=16\n')
    R.main(['force', cfg, '--boards', boards, '--ota-mode', 'enforce'])
    lines = lines_of(cfg)
    expect(all(R.line_of(k, 'y' if k == R.MODES['enforce'] else 'n') in lines for k in R.OTA),
           'force: the OTA lines go into a file that does not have them')
    expect(R.main(['force', cfg, '--boards', boards, '--ota-mode', 'strict']) == 1, 'force: stops for a wrong mode')
    expect(R.main(['force', cfg, '--boards', boards, '--ota-mode', '']) == 1, 'force: stops for no mode')
    expect(R.main(['force', os.path.join(tmp, 'none'), '--boards', boards, '--ota-mode', 'report']) == 1,
           'force: stops with no sdkconfig')


def test_check(tmp):
    h = os.path.join(tmp, 'sdkconfig.h')
    write(h, '#define CONFIG_TFN_OTA_SIG_ENFORCE 1\n#define CONFIG_X 1\n')
    expect(R.main(['check', h, '--ota-mode', 'enforce']) == 0, 'check: an ENFORCE build is ENFORCE')
    expect(R.main(['check', h, '--ota-mode', 'report']) == 1, 'check: an ENFORCE build is not REPORT')
    write(h, '#define CONFIG_TFN_OTA_SIG_REPORT 1\n')
    expect(R.main(['check', h, '--ota-mode', 'enforce']) == 1, 'check: a lost ENFORCE stops the job')
    expect(R.main(['check', h, '--ota-mode', 'report']) == 0, 'check: a REPORT build is REPORT')
    write(h, '#define CONFIG_TFN_OTA_SIG_REPORT 1\n#define CONFIG_TFN_OTA_SIG_OFF 1\n')
    expect(R.main(['check', h, '--ota-mode', 'report']) == 1, 'check: OFF too = a fault')
    expect(R.main(['check', os.path.join(tmp, 'none.h'), '--ota-mode', 'report']) == 1, 'check: no file = a fault')


def test_release_values(tmp):
    boards = os.path.join(tmp, 'boards_rel.json')
    write(boards, '{"force_cfg": {"CONFIG_LWIP_MAX_SOCKETS": "16"}, "release_cfg": {"_note": "x", '
                  '"CONFIG_TFN_POOL_STRICT": "n", "CONFIG_TFN_X_COUNT": "3"}}')
    cfg = os.path.join(tmp, 'sdkconfig_rel')
    write(cfg, 'CONFIG_LWIP_MAX_SOCKETS=10\n# default:\nCONFIG_TFN_POOL_STRICT=y\n')
    rc = R.main(['force', cfg, '--boards', boards, '--ota-mode', 'enforce'])
    lines = lines_of(cfg)
    expect(rc == 0 and '# CONFIG_TFN_POOL_STRICT is not set' in lines and 'CONFIG_TFN_X_COUNT=3' in lines and
           'CONFIG_LWIP_MAX_SOCKETS=16' in lines and '# default:' not in lines,
           'release_cfg: force writes the release values (also a value that the file does not have)')
    rel = R.release_values(boards)
    values = dict(R.board_values(boards), **rel)
    values.update(R.ota_values('enforce'))
    expect(R.force(cfg, values, R.OTA + tuple(rel)) == 0, 'release_cfg: a second run changes nothing')
    h = os.path.join(tmp, 'sdkconfig_rel.h')
    write(h, '#define CONFIG_TFN_OTA_SIG_ENFORCE 1\n#define CONFIG_TFN_X_COUNT 3\n')
    expect(R.main(['check', h, '--ota-mode', 'enforce', '--boards', boards]) == 0, 'release_cfg: check OK')
    write(h, '#define CONFIG_TFN_OTA_SIG_ENFORCE 1\n#define CONFIG_TFN_X_COUNT 3\n#define CONFIG_TFN_POOL_STRICT 1\n')
    expect(R.main(['check', h, '--ota-mode', 'enforce', '--boards', boards]) == 1,
           'release_cfg: check finds an option that must be off')
    write(h, '#define CONFIG_TFN_OTA_SIG_ENFORCE 1\n#define CONFIG_TFN_X_COUNT 4\n')
    expect(R.main(['check', h, '--ota-mode', 'enforce', '--boards', boards]) == 1,
           'release_cfg: check finds a number that is not the release value')
    expect(R.main(['check', h, '--ota-mode', 'enforce']) == 0, 'release_cfg: check with no --boards: the OTA mode only')
    write(boards, '{"release_cfg": {"CONFIG_TFN_OTA_SIG_OFF": "y"}}')
    expect(R.main(['force', cfg, '--boards', boards, '--ota-mode', 'enforce']) == 1,
           'release_cfg: an OTA option in release_cfg stops the tool')
    real_boards = os.path.join(ROOT, 'boards.json')
    real = R.release_values(real_boards)
    expect(real.get('CONFIG_TFN_POOL_STRICT') == 'n', 'release_cfg: boards.json has CONFIG_TFN_POOL_STRICT n')
    write(cfg, 'CONFIG_LWIP_MAX_SOCKETS=10\n')
    rc = R.main(['force', cfg, '--boards', real_boards, '--ota-mode', 'report'])
    expect(rc == 0 and all(R.line_of(k, v) in lines_of(cfg) for k, v in real.items()),
           'release_cfg: the values of the real boards.json go into a sdkconfig')


def test_keys(tmp):
    img = os.path.join(tmp, 'app.bin')
    with open(img, 'wb') as f:
        f.write(b'\xe9' + os.urandom(3000) + bytes.fromhex(KEY_A) + os.urandom(77) + bytes.fromhex(KEY_S) + b'\0' * 9)
    expect(R.main(['keys', img, '--ota-pub', KEY_A, '--store-pub', KEY_S]) == 0, 'keys: the image holds both keys')
    expect(R.main(['keys', img, '--ota-pub', KEY_A.upper()]) == 0, 'keys: a key in capitals')
    expect(R.main(['keys', img, '--ota-pub', KEY_B]) == 1, 'keys: another OTA key stops the job')
    expect(R.main(['keys', img, '--ota-pub', KEY_A, '--ota-pub2', KEY_B]) == 1,
           'keys: a second OTA key that the image does not hold stops the job')
    expect(R.main(['keys', img, '--ota-pub', KEY_A, '--store-pub', KEY_B]) == 1,
           'keys: another store key stops the job')
    expect(R.main(['keys', img, '--ota-pub', KEY_A[:-2]]) == 1, 'keys: a short key stops the job')
    expect(R.main(['keys', os.path.join(tmp, 'none.bin'), '--ota-pub', KEY_A]) == 1, 'keys: no image = a fault')


def compiler(tmp):
    """A C compiler that works, or ''."""
    t = os.path.join(tmp, 'cc_probe.c')
    write(t, 'int x;\n')
    for cc in [os.environ.get('CC', '')] + ['gcc', 'clang', 'cc']:
        if cc and shutil.which(cc):
            try:
                r = subprocess.run([cc, '-fsyntax-only', t], capture_output=True, timeout=60)
            except (OSError, subprocess.SubprocessError):
                continue
            if r.returncode == 0:
                return cc
    return ''


def ota_key_check():
    """The lines of main/ota.c from the OTA mode to the end of its key check (#error)."""
    text = read(os.path.join(ROOT, 'main', 'ota.c'))
    m = re.search(r'^#if \(defined\(CONFIG_TFN_DEVICE_PROTECTION\).*?^#error[^\n]*\n#endif\n#endif\n', text,
                  re.M | re.S)
    if not m:
        raise SystemExit('FAIL: the key check of main/ota.c is not found (the test must follow its change)')
    return m.group(0)


def test_c(tmp):
    cc = compiler(tmp)
    if not cc:
        print('SKIP the C check: no C compiler (set CC)')
        return
    block = ota_key_check()
    for name, mode, keys, local, good in (('ENFORCE with the release keys', 'ENFORCE', True, False, True),
                                          ('ENFORCE with no key', 'ENFORCE', False, False, False),
                                          ('REPORT with no key', 'REPORT', False, False, True),
                                          ('ENFORCE, tfn_local.h and the release keys', 'ENFORCE', True, True, True)):
        d = tempfile.mkdtemp(dir=tmp)
        shutil.copyfile(os.path.join(ROOT, 'main', 'tfn_defaults.h'), os.path.join(d, 'tfn_defaults.h'))
        if keys:
            R.main(['header', '--out', os.path.join(d, 'tfn_release_keys.h'), '--store-pub', KEY_S, '--ota-pub', KEY_A])
        if local:
            write(os.path.join(d, 'tfn_local.h'), '#pragma once\n#define TFN_OTA_PUBKEY1_BYTES %s\n' % R.c_bytes(KEY_B))
        want = KEY_B if local else KEY_A
        src = ('#define CONFIG_TFN_OTA_SIG_%s 1\n#include "tfn_defaults.h"\n%s\n' % (mode, block) +
               ('static const unsigned char k1[] = {TFN_OTA_PUBKEY1_BYTES};\n'
                '_Static_assert(sizeof(k1) == 65 && k1[1] == 0x%s, "the key of slot 1");\n'
                'const unsigned char *keep = k1;\n' % want[2:4] if keys else 'int keep;\n'))
        write(os.path.join(d, 't.c'), src)
        r = subprocess.run([cc, '-fsyntax-only', '-Wall', '-Werror', '-I', d, os.path.join(d, 't.c')],
                           capture_output=True, text=True, timeout=60)
        out = (r.stdout + r.stderr).strip()
        ok = (r.returncode == 0) == good and (good or 'needs at least one OTA key' in out)
        expect(ok, 'C (%s): %s%s' % (cc, name, '' if ok else '\n' + out[-1500:]))


def main():
    with tempfile.TemporaryDirectory(prefix='tfn_relcfg_') as tmp:
        test_header(tmp)
        test_force(tmp)
        test_check(tmp)
        test_release_values(tmp)
        test_keys(tmp)
        test_c(tmp)
    print('RELEASE CFG TESTS %s' % ('OK' if not FAILS else 'FAULT (%d)' % len(FAILS)))
    return 1 if FAILS else 0


if __name__ == '__main__':
    sys.exit(main())
