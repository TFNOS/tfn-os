# TFN OS SDK: the descriptions of the store (sdk/docs/store.md). Each app has its description in the
# store.json of its folder; mk_app.py puts it into each package (desc=, desc_he=). This tool sends the descriptions of
# all the apps to the manager with no new package, and finds the apps of the store that have no description.
#
#   python sdk/tools/store_desc.py                  check: each app of the store has a valid description (exit 1: no)
#   python sdk/tools/store_desc.py --send           send all the descriptions to the store of the manager
#   python sdk/tools/store_desc.py --manager http://<manager>:<port> [--send]
import argparse
import json
import os
import sys
import urllib.request

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mk_app import DESC_CHARS, DESC_MAX     # noqa: E402  (the same rules as the packages)
import store_conf  # noqa: E402


def collect():
    # {id: (en, he, the path of its store.json)} of apps/*/store.json and sdk/examples/*/store.json
    out = {}
    for top in (os.path.join(ROOT, 'apps'), os.path.join(ROOT, 'sdk', 'examples')):
        if not os.path.isdir(top):
            continue
        for d in sorted(os.listdir(top)):
            p = os.path.join(top, d, 'store.json')
            if not os.path.exists(p):
                continue
            for aid, v in json.load(open(p, encoding='utf-8')).items():
                out[aid] = (' '.join(str(v.get('en', '')).split()), ' '.join(str(v.get('he', '')).split()),
                            os.path.relpath(p, ROOT).replace(os.sep, '/'))
    return out


def faults(aid, en, he):
    f = []
    if not en:
        f.append('no English description')
    for lang, t in (('en', en), ('he', he)):
        if len(t) > DESC_MAX:
            f.append('%s: %d characters (%d at most)' % (lang, len(t), DESC_MAX))
        bad = sorted({c for c in t if ord(c) not in DESC_CHARS})
        if bad:
            f.append('%s: characters that the board fonts do not have: %s' % (lang, ' '.join('U+%04X' % ord(c) for c in bad)))
    return f


def default_manager():
    # the manager of the store setup (store_conf.py: the environment variable TFN_MANAGER or the site module)
    return store_conf.manager_url()


def main():
    ap = argparse.ArgumentParser(description='The descriptions of the store (sdk/docs/store.md).')
    ap.add_argument('--manager', default=default_manager(), help='the manager (default: the one of the store setup, store_conf.py)')
    ap.add_argument('--send', action='store_true', help='send the descriptions to the store of the manager')
    args = ap.parse_args()
    if not args.manager:
        sys.exit('give --manager http://<host>:<port> (or set TFN_MANAGER)')
    descs = collect()
    try:
        with urllib.request.urlopen(args.manager + '/api/store', timeout=20) as r:
            store = {a['id']: a for a in json.load(r)['apps']}
    except Exception as e:
        sys.exit('the manager does not answer (%s): %s' % (args.manager, e))
    bad = 0
    for aid in sorted(store):
        if aid not in descs:
            print('NO DESCRIPTION  %-12s %s' % (aid, store[aid].get('name', '')))
            bad += 1
            continue
        f = faults(aid, *descs[aid][:2])
        if f:
            print('FAULT           %-12s %s: %s' % (aid, descs[aid][2], '; '.join(f)))
            bad += 1
        elif not descs[aid][1]:
            print('NO HEBREW       %-12s %s' % (aid, descs[aid][2]))
    print('%d apps in the store, %d descriptions in the repo, %d faults' % (len(store), len(descs), bad))
    if args.send:
        body = {aid: {'en': en, 'he': he} for aid, (en, he, _) in descs.items()
                if aid in store and not faults(aid, en, he)}
        req = urllib.request.Request(args.manager + '/api/store/desc', data=json.dumps(body).encode('utf-8'), method='POST',
                                     headers={'Content-Type': 'application/json'})
        with urllib.request.urlopen(req, timeout=30) as r:
            j = json.load(r)
        print('sent %d descriptions: the manager set %d%s' % (len(body), j.get('n', 0), (': ' + j['msg']) if j.get('msg') else ''))
        if not j.get('ok'):
            bad += 1
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
