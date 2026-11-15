#!/usr/bin/env python3
"""The host unit tests of the core: each test is one C file in tests/unit/ with the main/ sources that it needs.
The fake ESP-IDF headers are in tests/unit/fake/. The compiler: $CC, else clang, else gcc (with ASan and UBSan when
the compiler has them; also on Windows with the clang of llvm-mingw).
The tests of the signed OTA (test_ota_sig.c, test_ota_flow.c) also read the vectors that tests/unit/test_ota_sig.py
makes with sdk/tools/fw_sign.py (the openssl program): this script runs that step first.
It also runs the Python tests of the pools: test_pool_crypto.py (pool_crypto.py, pool.py and the shared vectors
tests/unit/vectors/pool_v1.json), test_pool_tool.py (pool_tool.py against fake boards) and test_pool_gate.py (the P4
gate tests/pool/gate.py against fake boards and a fake manager), and test_dev_tools.py (tfn.py build --version and the
files of the step gitleaks of tests/ci_local.py).
The test of the static store
(test_sstore.c) reads the sample store in tests/unit/sstore/ (tests/unit/sstore_sample.py makes it again).
test_input_gate.c runs the real touch task of main/input.c (the fakes of tests/unit/fake_input/): the rule of a
consent. test_pub_web.c compiles main/tfn_pub.c (the fakes of tests/unit/fake_pub/): the answer of GET /api/pub and
the prompt of a key. test_sysui_ask.c compiles main/sysui.c and main/app_settings.c (the fakes of
tests/unit/fake_sysui/; the real fonts of main/kfont.c for the width of the texts): the consent question, the fit of
its texts in each frame, and the question of SETUP > DEVELOPER MODE. Its second build (test_sysui_ask_round) is for a
round board kind: the round frames. test_setup_pages.c compiles
the two consent pages of SETUP, main/tfn_pub_page.c and main/pool_page.c (the ESP-IDF fakes of tests/unit/fake_sysui/).
The test of the app upload of the board web API (test_tfn_web.c: op=put and op=commit of main/tfn_web.c) uses the
fakes of tests/unit/fake_web/ and the work folder tfn_unit_web/ in the temp folder of the system. The reason: a sync
tool (Dropbox) can hold the files of a work folder in the repo, and then the test cannot remove its upload folders.

Usage: python tests/unit/run.py
"""
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
OUT = os.path.join(ROOT, 'tests', 'host', 'unit')
VEC = os.path.join(OUT, 'ota_vec')  # the vectors of the signed OTA (test_ota_sig.py)
# the vectors of the pools (the manager side, tests/unit/test_pool_crypto.py, writes them; test_pool_core.c checks
# them when the file is there)
POOL_VEC = os.path.join(HERE, 'vectors', 'pool_v1.json')
SSTORE = os.path.join(HERE, 'sstore')  # the sample static store of sdk/tools/store_index.py (sstore_sample.py)
# the fake parts of the link test (ESP-IDF, FreeRTOS, lwIP, the OS headers that link.c and lan.c use; a fake pool
# service): only for test_pool_link.c, before main/ in the include folders. A test with its own include folders
# compiles copies of its main/ sources (in tests/host/unit/<test>_src/): an include with quotes looks in the folder of
# the source first, and main/ has the real headers.
FAKE_LINK = 'tests/unit/fake_link'
# the fake parts of the input test (FreeRTOS, the GPIO driver, the board, the screen values and the store that
# main/input.c uses): only for test_input_gate.c (it runs the real touch task of input.c)
FAKE_INPUT = 'tests/unit/fake_input'
# the fake parts of the test of /api/pub (the private header of the installed apps, the system layer, the web
# password): only for test_pub_web.c (it compiles main/tfn_pub.c)
FAKE_PUB = 'tests/unit/fake_pub'
# the fake parts of the test of the question of the system layer (the private header of sysui.c, ESP-IDF, FreeRTOS,
# the other parts of SETUP): for test_sysui_ask.c (it compiles main/sysui.c and main/app_settings.c) and, for its
# ESP-IDF headers, test_setup_pages.c
FAKE_SYSUI = 'tests/unit/fake_sysui'
LINK_SRCS = ['main/link.c', 'main/link_meet.c', 'main/lan.c', 'main/friends.c', 'main/walkie_call.c', FAKE_LINK + '/fake_board.c',
             FAKE_LINK + '/pool_fake.c', 'main/tfn_pool_core.c']
# the fake parts of the upload test of tfn_web.c (the ESP-IDF headers that it uses; tfn_app.h with the two app
# folders in the work folder of the test)
FAKE_WEB = 'tests/unit/fake_web'
# the sources of test_sysui_ask.c, and the options of its second build: a round board kind (the round frames)
SYSUI_SRCS = ['main/sysui.c', 'main/app_settings.c', 'main/kfont.c', FAKE_SYSUI + '/setup_fakes.c']
ROUND_KIND = {'name': 'test_sysui_ask_round', 'defs': ['-DCONFIG_TFN_TARGET_AMOLED175=1']}
# (the test, the sources that it compiles with, the arguments of the run[, the include folders before main/[, the
# options of the build: 'name' = the name of the program, 'defs' = more arguments of the compiler]])
TESTS = [
    ('test_web_util.c', ['main/web_util.c'], []),
    ('test_ui_hit.c', [], []),
    ('test_utf8.c', [], []),
    ('test_ota_sig.c', ['main/ota_sig.c', 'tests/unit/host_crypto.c'], [VEC]),
    ('test_ota_flow.c', ['main/ota_flow.c', 'main/ota_sig.c', 'tests/unit/host_crypto.c'], [VEC]),
    ('test_tfn_id.c', [], []),
    ('test_pub.c', ['main/tfn_pub_core.c'], []),
    ('test_pool_core.c', ['main/tfn_pool_core.c', 'tests/unit/host_crypto.c'], [POOL_VEC]),
    ('test_pool_flow.c', ['main/tfn_pool_flow.c', 'main/tfn_pool_core.c', 'tests/unit/host_crypto.c'], [POOL_VEC]),
    ('test_sstore.c', ['main/tfn_sstore_core.c', 'main/tfn_pub_core.c', 'tests/unit/host_crypto.c'], [SSTORE]),
    ('test_pool_link.c', LINK_SRCS, [POOL_VEC], [FAKE_LINK]),
    ('test_link_chan.c', LINK_SRCS, [], [FAKE_LINK]),
    ('test_link_tx.c', LINK_SRCS, [], [FAKE_LINK]),
    ('test_link_meet.c', ['main/link_meet.c'], []),
    ('test_input_gate.c', ['main/input.c'], [], [FAKE_INPUT]),
    ('test_pub_web.c', ['main/tfn_pub.c', 'main/tfn_pub_core.c', 'main/web_util.c', 'tests/unit/host_crypto.c'], [],
     [FAKE_PUB]),
    ('test_sysui_ask.c', SYSUI_SRCS, [], [FAKE_SYSUI]),
    ('test_sysui_ask.c', SYSUI_SRCS, [], [FAKE_SYSUI], ROUND_KIND),
    ('test_setup_pages.c', ['main/tfn_pub_page.c', 'main/pool_page.c', 'main/tfn_pub_core.c'], [], [FAKE_SYSUI]),
    ('test_tfn_web.c', ['main/tfn_web.c'], [os.path.join(tempfile.gettempdir(), 'tfn_unit_web')], [FAKE_WEB]),
]

# the Python tests of the pools, of the WiFi install of the SDK and of the developer tools (each prints one line that
# starts with its tag)
PY_TESTS = [('test_pool_crypto.py', 'POOL CRYPTO PY'), ('test_pool_tool.py', 'POOL TOOL PY'),
            ('test_pool_gate.py', 'POOL GATE PY'), ('test_pkg_push.py', 'PKG PUSH PY'),
            ('test_dev_tools.py', 'DEV TOOLS PY')]
# the tests of poolmgr/: each file tests/unit/test_poolmgr*.py, with the tag of its name (test_poolmgr_ota.py prints
# "POOLMGR OTA PY OK ...", test_poolmgr.py prints "POOLMGR PY OK ...")
PY_TESTS += [(n, n[5:-3].replace('_', ' ').upper() + ' PY') for n in sorted(os.listdir(HERE))
             if n.startswith('test_poolmgr') and n.endswith('.py')]


def compiler():
    cc = os.environ.get('CC') or shutil.which('clang') or shutil.which('gcc')
    if not cc:
        sys.exit('no C compiler (set CC)')
    return cc


def source(test, s, inc):
    """The path of a source to compile: a main/ source of a test with its own include folders = a copy."""
    if not inc or not s.startswith('main/'):
        return os.path.join(ROOT, s)
    d = os.path.join(OUT, test[:-2] + '_src')
    os.makedirs(d, exist_ok=True)
    out = os.path.join(d, os.path.basename(s))
    shutil.copyfile(os.path.join(ROOT, s), out)
    return out


def build(cc, test, srcs, exe, inc=(), defs=()):
    first = [a for d in inc for a in ('-I', os.path.join(ROOT, d))]
    base = [cc, '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror'] + list(defs) + first + [
        '-I', os.path.join(HERE, 'fake'), '-I', os.path.join(ROOT, 'main'), os.path.join(HERE, test)] + [
        source(test, s, inc) for s in srcs]
    r = subprocess.run(base + ['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-o', exe, '-lm'],
                       capture_output=True, text=True)
    if r.returncode == 0:
        return '', 'ASan + UBSan'
    r = subprocess.run(base + ['-o', exe, '-lm'], capture_output=True, text=True)
    return ('' if r.returncode == 0 else (r.stdout + r.stderr)[-2000:]), 'no sanitizer'


def vectors():
    """The vectors of the signed OTA: tests/unit/test_ota_sig.py (it also checks sdk/tools/fw_sign.py)."""
    r = subprocess.run([sys.executable, os.path.join(HERE, 'test_ota_sig.py'), '--out', VEC], capture_output=True,
                       encoding='utf-8', errors='replace')
    for ln in r.stdout.splitlines():
        if ln.startswith(('OTA SIG PY', 'FAIL', 'build_dev', '(no ')):
            print(ln)
    if r.returncode:
        print(r.stderr[-2000:])
    return r.returncode == 0


def py_tests():
    """The Python tests of PY_TESTS: the number that failed."""
    bad = 0
    for test, tag in PY_TESTS:
        r = subprocess.run([sys.executable, os.path.join(HERE, test)], capture_output=True, encoding='utf-8',
                           errors='replace')
        done = False
        for ln in r.stdout.splitlines():
            if ln.startswith((tag, 'FAIL', '(no ')):
                print(ln)
            done = done or ln.startswith(tag + ' OK')
        if r.returncode or not done:
            print('%s FAILED (exit %d, no "%s OK" line)\n%s' % (test, r.returncode, tag, r.stderr[-2000:]))
            bad += 1
    return bad


def main():
    cc = compiler()
    os.makedirs(OUT, exist_ok=True)
    bad = 0 if vectors() else 1
    bad += py_tests()
    for test, srcs, args, *more in TESTS:
        opt = more[1] if len(more) > 1 else {}
        exe = os.path.join(OUT, opt.get('name', test[:-2]) + ('.exe' if sys.platform == 'win32' else ''))
        err, how = build(cc, test, srcs, exe, more[0] if more else (), opt.get('defs', ()))
        if err:
            bad += 1
            print('BUILD FAIL %s\n%s' % (test, err))
            continue
        r = subprocess.run([exe] + args, capture_output=True, encoding='utf-8', errors='replace')
        out = (r.stdout + r.stderr).strip()
        print(('%s [%s]' % (out, how)).encode('ascii', 'backslashreplace').decode('ascii'))
        if r.returncode:
            bad += 1
    print('UNIT TESTS %s (%d + the vectors + %d Python tests)' % ('OK' if not bad else '%d FAILED' % bad, len(TESTS),
                                                                  len(PY_TESTS)))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
