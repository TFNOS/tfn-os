# TFN OS: the DATA PACK of an app in the store of the manager (big files for the SD card: a city, a map, music).
# Each file goes to api/store/data with its place on the card; the keeper of the manager then copies the missing files
# to each board that has the app and an SD card (never while the app runs). MANUAL.md "The data packs of the store".
#   python sdk/tools/store_data.py <app id> <local file>=<card path> [...] [--mgr http://<manager>:<port>]
#   python sdk/tools/store_data.py beeper out/world.bin=/beeper/world.bin \
#                                         out/ground.bpg=/beeper/ground.bpg
#   python sdk/tools/store_data.py <app id> --rm [card path]          (one file out, or all of the data of the app)
import argparse, json, os, sys, time, urllib.parse, urllib.request

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import store_conf  # noqa: E402


def call(url, data=None, timeout=60):
    req = urllib.request.Request(url, data=data, method='POST')
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.loads(r.read().decode('utf-8', 'replace'))


def default_manager():
    # the manager of the store setup (store_conf.py: the environment variable TFN_MANAGER or the site module)
    return store_conf.manager_url()


def main():
    ap = argparse.ArgumentParser(description='the data pack of an app in the store of the manager')
    ap.add_argument('id', help='the id of the app (a-z 0-9 _), as in its package')
    ap.add_argument('files', nargs='*', help='<local file>=<card path>')
    ap.add_argument('--mgr', default=default_manager(), help='the manager (default: the one of '
                    'the store setup, store_conf.py)')
    ap.add_argument('--rm', nargs='?', const='', default=None, help='take a file (a card path) or all of the data out')
    a = ap.parse_args()
    if not a.mgr:
        ap.error('no manager: give --mgr http://<manager>:<port> (or set TFN_MANAGER)')
    base = a.mgr.rstrip('/')
    if a.rm is not None:
        r = call('%s/api/store/data_rm?id=%s&path=%s' % (base, a.id, urllib.parse.quote(a.rm, safe='/')), b'')
        print(r.get('msg'))
        return 0 if r.get('ok') else 1
    if not a.files:
        ap.error('no files')
    bad = 0
    for spec in a.files:
        if '=' not in spec:
            print('%s: <local file>=<card path> please' % spec)
            bad += 1
            continue
        src, dst = spec.split('=', 1)
        data = open(src, 'rb').read()
        t0 = time.time()
        url = '%s/api/store/data?id=%s&path=%s&t=%d' % (base, a.id, urllib.parse.quote(dst, safe='/'), int(os.path.getmtime(src)))
        try:
            r = call(url, data, 120 + len(data) // 200000)
        except Exception as e:
            r = {'ok': False, 'msg': str(e)}
        print('%-40s %10d bytes %5.1f s: %s' % (dst, len(data), time.time() - t0, r.get('msg')))
        bad += 0 if r.get('ok') else 1
    print('STORE DATA OK' if not bad else 'STORE DATA: %d FAILED' % bad)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
