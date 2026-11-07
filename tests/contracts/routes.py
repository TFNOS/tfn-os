"""The HTTP contract: each route of the board web server (the method and the URI of each httpd_uri_t).

The manager and the web pages call these routes. A lost route (or a route with a changed method) stops them.
The reader finds each initializer with '.uri = "..."' in main/, libs/ and apps/ (an app can add a route too), and
takes '.method' and '.is_websocket' from the same initializer.
"""
import os
import re

import csrc

PREFIXES = ('main/', 'libs/', 'apps/')
URI_RE = re.compile(r'\.uri\s*=\s*"([^"]*)"')


def _brace_around(text, pos):
    """The text of the innermost { ... } that holds pos."""
    depth = 0
    for k in range(pos, -1, -1):
        c = text[k]
        if c == '}':
            depth += 1
        elif c == '{':
            if depth == 0:
                end = csrc.match_close(text, k, '{', '}')
                return text[k:end + 1] if end > 0 else text[k:pos + 200]
            depth -= 1
    return text[pos:pos + 200]


def extract():
    routes = set()
    where = {}
    for rel in csrc.tracked(PREFIXES):
        with open(os.path.join(csrc.ROOT, rel), encoding='utf-8', errors='replace') as f:
            text = csrc.strip_comments(f.read())
        for m in URI_RE.finditer(text):
            init = _brace_around(text, m.start())
            mm = re.search(r'\.method\s*=\s*(\w+)', init)
            method = mm.group(1).replace('HTTP_', '') if mm else '?'
            ws = ' (websocket)' if re.search(r'\.is_websocket\s*=\s*true', init) else ''
            r = '%s %s%s' % (method, m.group(1), ws)
            routes.add(r)
            where.setdefault(r, set()).add(rel)
    return {
        'routes': sorted(routes),
        'files': {r: sorted(v) for r, v in sorted(where.items())},
    }


CONTRACT = ('routes',)
INFO = ('files',)
