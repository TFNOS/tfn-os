#!/usr/bin/env python3
# The classes of the store: main/apps.json "class": "b" marks a lesser game (the manager does not install it by
# default, and its web page marks it). This tool sends the list
# to the manager (POST api/store/class: store/class.json), which then installs no class B game by itself (the profiles,
# the slim transition, a new board) and shows "CLASS B" on its page.
#   python sdk/tools/store_class.py            show the list (the store id of each name)
#   python sdk/tools/store_class.py --send     send it to the manager (the store setup of store_conf.py, as mk_app.py)
import json
import os
import sys
import urllib.request

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mk_app import default_manager  # noqa: E402


def main():
    doc = json.load(open(os.path.join(ROOT, 'main', 'apps.json'), encoding='utf-8'))
    names = [a['name'].upper() for a in doc['apps'] if a.get('class') == 'b']
    mgr = default_manager()
    with urllib.request.urlopen(mgr + '/api/store', timeout=30) as r:
        store = json.loads(r.read())['apps']
    byname = {a['name'].upper(): a['id'] for a in store}
    cls = {byname[n]: 'b' for n in names if n in byname}
    for n in names:
        print('%-16s %s' % (n, byname.get(n, '(not in the store)')))
    if '--send' in sys.argv:
        req = urllib.request.Request(mgr + '/api/store/class', data=json.dumps({'class': cls}).encode(), method='POST',
                                     headers={'Content-Type': 'application/json'})
        with urllib.request.urlopen(req, timeout=30) as r:
            print('CLASS SENT:', r.read().decode())


if __name__ == '__main__':
    main()
