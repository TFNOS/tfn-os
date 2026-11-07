# TFN OS SDK: make the API reference sdk/docs/api.md from the ABI (main/tfn_abi.json) and the headers of main/.
#
#   python sdk/tools/gen_api_doc.py            write sdk/docs/api.md
#   python sdk/tools/gen_api_doc.py --check    exit 1 when sdk/docs/api.md is not the same as a new one
#
# For each ABI name the tool finds its declaration in main/tfn/*.h, else main/*.h (a top-level line, not a line inside a function body or a
# struct) and the comment lines right above it. A name with no comment of its own gets the comment of the block of
# declarations that it is in (no empty line between them). The names go into groups by header. The names that no header
# of main/ declares (the C library, the compiler runtime, FreeRTOS and ESP-IDF) go into their own groups.
# The tool also lists the "static inline" helpers of the main headers: they are in the header, so they need no ABI name.
import argparse
import json
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
MAIN = os.path.join(ROOT, 'main')
OUT = os.path.join(ROOT, 'sdk', 'docs', 'api.md')
KINDS = ('lcd349', 'cores3', 'am18', 'watcher', 'tab5')

# the headers that the tool does not read (music data)
SKIP = re.compile(r'^(.*_music)\.h$')

# the groups: (title, the headers); the order of this list is the order of the document
GROUPS = [
    ('App system', ['app.h']),
    ('Graphics', ['gfx.h']),
    ('Touch input', ['input.h']),
    ('Board', ['board.h']),
    ('Motion sensor', ['imu.h']),
    ('Sound', ['audio.h']),
    ('Start tunes', ['tunes.h']),
    ('Settings and saves', ['settings.h']),
    ('Files', ['storage.h', 'export.h']),
    ('Fonts', ['mfont.h', 'kfont.h', 'strokefont.h']),
    ('UI kit', ['uikit.h']),
    ('System layer', ['sysui.h']),
    ('Music engine', ['music.h', 'midi_bg.h', 'midifile.h', 'drumkit.h', 'instr.h']),
    ('MIDI ports', ['usb_midi.h', 'ble_midi.h']),
    ('Network and radio', ['wifi.h', 'link.h', 'friends.h', 'net2.h', 'duo.h', 'duel.h', 'ha.h']),
    ('Drawing modules', ['toon.h', 'scn.h', 'qr.h']),
    ('Network and data modules', ['tfnet.h', 'tfmqtt.h', 'zimread.h']),
]
HEADER_TITLE = {h: t for t, hs in GROUPS for h in hs}

# the headers whose static inline helpers go into the list of helpers
HELPER_HEADERS = ['app.h', 'gfx.h', 'input.h', 'board.h', 'settings.h', 'uikit.h', 'sysui.h']

# the headers whose types (typedef, enum) and constants (#define) go into the list of types
TYPE_HEADERS = ['app.h', 'input.h', 'gfx.h', 'settings.h', 'audio.h', 'tunes.h', 'imu.h', 'mfont.h', 'kfont.h',
                'sysui.h', 'storage.h']
# the types and constants of the system only (main.c, the touch task), not for apps
SKIP_TYPE = re.compile(r'\b(ARCADE_\w*|PULL_\w+|input_zone_t|input_pull_t|ZONE_HOLD|PANEL_NONE|SK_BOOT)\b')

# a note for the helpers that have no comment in their header
HELPER_NOTES = {
    'rgb': 'A colour of the frame buffer from 8-bit red, green and blue (RGB565, the two bytes swapped for the SPI bus).',
    'fcos': 'The fast cosine: `fsin(x + pi / 2)`.',
    'orient_is_landscape': 'True for ORIENT_LAND_A and ORIENT_LAND_B.',
}

# the names with no header of main/: the group of each (the first rule that fits)
EXTERN_RULES = [
    ('Other firmware names', lambda n: n == 'app_menu' or n.startswith('SCN_')),
    ('Compiler runtime (automatic)', lambda n: n.startswith('__')),
    ('FreeRTOS', lambda n: re.match(r'^(x|v|ux|pv)(Task|Queue|Port|Semaphore)', n) is not None),
    ('ESP-IDF', lambda n: n.startswith(('esp_', 'heap_caps_', 'httpd_', 'jd_', 'tinfl_'))),
    ('C library: memory', lambda n: n in ('malloc', 'calloc', 'realloc', 'free')),
    ('C library: files', lambda n: n in ('fopen', 'fclose', 'fread', 'fwrite', 'fseek', 'ftell', 'remove', 'rename',
                                         'unlink', 'mkdir', 'stat', 'opendir', 'readdir', 'closedir')),
    ('C library: text', lambda n: n.startswith(('str', 'mem')) or n in ('snprintf', 'sprintf', 'atoi', 'atof',
                                                                       'tolower', 'toupper', '_ctype_b')),
    ('C library: other', lambda n: n in ('qsort', 'time')),
    ('C library: math', lambda n: True),
]

# what the system does with some names of the C library and FreeRTOS for an app file (main/tfn_app.c)
TRACKED = {}
for _n in ('malloc', 'calloc', 'realloc', 'free', 'heap_caps_malloc', 'heap_caps_calloc', 'heap_caps_free'):
    TRACKED[_n] = 'the system notes each block of an app file and frees it when the app closes'
for _n in ('fopen', 'fclose'):
    TRACKED[_n] = 'the system closes the open files of an app file when the app closes'
for _n in ('xTaskCreatePinnedToCore', 'xTaskCreatePinnedToCoreWithCaps', 'vTaskDelete', 'vTaskDeleteWithCaps'):
    TRACKED[_n] = 'the system notes the tasks of an app file; while one runs, the code of the app stays in memory'


def strip_comment(line):
    i = line.find('//')
    return line if i < 0 else line[:i]


def parse_header(path):
    """The top-level statements of a header: a list of dict(start, text, comment, trail, names, inline, joined), and the
    #define lines: a list of dict(name, text, comment, trail)."""
    lines = open(path, encoding='utf-8', errors='replace').read().splitlines()
    out = []
    defines = []
    depth = 0           # the depth of { } (a struct, an enum, a function body)
    in_block = False    # inside /* */
    i = 0
    pending = []        # the comment lines right above the next statement
    block_comment = []  # the comment of the block of declarations (no empty line in between)
    while i < len(lines):
        raw = lines[i]
        s = raw.strip()
        if in_block:
            if '*/' in s:
                in_block = False
            i += 1
            continue
        if s.startswith('/*'):
            if '*/' not in s:
                in_block = True
            i += 1
            continue
        if depth == 0 and s.startswith('//'):
            c = re.sub(r'^-{2,}\s*|\s*-{2,}$', '', s[2:].strip())
            if c:
                pending.append(c)
            i += 1
            continue
        if not s:
            pending = []
            block_comment = []
            i += 1
            continue
        if s.startswith('#'):
            # a preprocessor line: it does not end the comment block of the declarations around it
            m = re.match(r'#\s*define\s+([A-Za-z_]\w*)(\(?)', s)
            if m and depth == 0 and not raw.rstrip().endswith('\\'):
                trail = raw[raw.find('//') + 2:].strip() if '//' in raw else ''
                defines.append({'name': m.group(1), 'text': strip_comment(raw).rstrip(), 'trail': trail,
                                'comment': pending[:] if pending else block_comment[:], 'macro': bool(m.group(2))})
                if pending:
                    block_comment = pending[:]
                pending = []
            while raw.rstrip().endswith('\\') and i + 1 < len(lines):
                i += 1
                raw = lines[i]
            i += 1
            continue
        if depth > 0:
            code = strip_comment(raw)
            depth += code.count('{') - code.count('}')
            i += 1
            continue
        # a top-level statement: up to a ';' at depth 0, or the end of a { } body
        start = i
        text, trail = [], []
        d = 0
        inline = False
        while i < len(lines):
            raw = lines[i]
            code = strip_comment(raw)
            if '//' in raw:
                trail.append(raw[raw.find('//') + 2:].strip())
            text.append(raw.rstrip())
            d += code.count('{') - code.count('}')
            i += 1
            if d <= 0 and (code.rstrip().endswith(';') or (code.count('}') and d == 0)):
                break
            if d == 0 and code.rstrip().endswith(')') and i < len(lines) and lines[i].strip().startswith('{'):
                continue
        joined = ' '.join(strip_comment(t) for t in text)
        if re.search(r'\bstatic\s+inline\b', joined):
            inline = True
        comment = pending[:] if pending else block_comment[:]
        own = bool(pending)
        if pending:
            block_comment = pending[:]
        pending = []
        names = statement_names(joined)
        out.append({'start': start + 1, 'text': text, 'comment': comment, 'own': own, 'trail': trail, 'names': names, 'inline': inline, 'joined': joined})
    return out, defines


def statement_names(code):
    """The names that a statement declares: a function name, or the names of an extern list."""
    names = set()
    head = code.split('{', 1)[0]
    m = re.search(r'([A-Za-z_]\w*)\s*\(', head)
    if m and not head.lstrip().startswith(('typedef', 'extern')):
        names.add(m.group(1))
    elif head.lstrip().startswith('extern'):
        body = re.sub(r'\[[^\]]*\]', '', head.rstrip(';'))
        body = re.sub(r'\b(extern|const|volatile|unsigned|signed|struct|static)\b', ' ', body)
        for part in body.split(','):
            w = re.findall(r'[A-Za-z_]\w*', part)
            if w:
                names.add(w[-1])
        # a function pointer in an extern line: extern void (*name)(...)
        for fm in re.finditer(r'\(\s*\*\s*([A-Za-z_]\w*)\s*\)', head):
            names.add(fm.group(1))
    return names


def decl_text(st):
    """The declaration as one clean block of code (with no trailing comments, no body)."""
    if st['inline']:
        first = strip_comment(st['text'][0]).rstrip()
        sig = first.split('{', 1)[0].rstrip()
        if not sig.endswith(')'):
            # a signature over more lines
            parts = []
            for t in st['text']:
                c = strip_comment(t).rstrip()
                parts.append(c.split('{', 1)[0].rstrip())
                if '{' in c or c.endswith(')'):
                    break
            sig = '\n'.join(p for p in parts if p.strip())
        return sig + ';'
    lines = [strip_comment(t).rstrip() for t in st['text']]
    return '\n'.join(l for l in lines if l.strip())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--check', action='store_true')
    args = ap.parse_args()
    abi = json.load(open(os.path.join(MAIN, 'tfn_abi.json'), encoding='utf-8'))
    syms = abi['symbols']
    # the public headers (main/tfn/), then the other headers of main/ (the extras) and of the shared modules
    # libs/<module>/ (midi_bg.h, toon.h ...) and of third_party/<module>/ (zstd.h)
    pub = os.path.join(MAIN, 'tfn')
    paths = {f: os.path.join(pub, f) for f in os.listdir(pub) if f.endswith('.h') and not SKIP.match(f)}
    for f in os.listdir(MAIN):
        if f.endswith('.h') and not SKIP.match(f) and f not in paths:
            paths[f] = os.path.join(MAIN, f)
    for top in ('libs', 'third_party'):
        libs = os.path.join(ROOT, top)
        for d in sorted(os.listdir(libs)) if os.path.isdir(libs) else []:
            if os.path.isdir(os.path.join(libs, d)):
                for f in sorted(os.listdir(os.path.join(libs, d))):
                    if f.endswith('.h') and not SKIP.match(f) and f not in paths:
                        paths[f] = os.path.join(libs, d, f)
    headers = sorted(paths)
    parsed, defs = {}, {}
    for h in headers:
        parsed[h], defs[h] = parse_header(paths[h])

    # each ABI name: the first header that declares it (the order: the headers of the groups, then the others)
    order = [h for _, hs in GROUPS for h in hs if h in parsed] + [h for h in headers if h not in HEADER_TITLE]
    found = {}
    for h in order:
        for st in parsed[h]:
            for n in st['names']:
                if n in syms and n not in found:
                    found[n] = (h, st)
    # the extra names (main/tfn_abi_extra.json) that no header of main/ declares: raw ESP-IDF names, a section of their own
    xf = os.path.join(MAIN, 'tfn_abi_extra.json')
    xnames = {e['name']: e for e in json.load(open(xf, encoding='utf-8')).get('names', [])} if os.path.exists(xf) else {}
    raw = sorted(n for n in syms if n not in found and n in xnames)
    # the app_t of an app (app_menu): app.h declares them with the ARCADE_APP list
    other = sorted(n for n in syms if n not in found and n not in xnames)

    md = []
    md.append('# TFN OS API reference')
    md.append('')
    md.append('> A tool makes this file. Do not change it by hand. To make it again: '
              '`python sdk/tools/gen_api_doc.py`.')
    md.append('')
    md.append('**ABI version %d rev %d: %d names.** An app file can call only these names. `sdk/tools/mk_app.py` stops '
              'when an app needs another name and writes a request for it ([raw.md](raw.md)). Inside one ABI version, the '
              'list only gets new names; each addition gives the next revision.' % (abi['version'], abi.get('rev', 0), len(syms)))
    md.append('')
    md.append('The source of each entry is the public header in `main/tfn/` and the comment above the declaration. A name that '
              'only some board kinds have shows the kinds. All %d kinds have each other name (%s).' % (len(KINDS), ', '.join(KINDS)))
    md.append('')
    md.append('The header helpers at the end (`static inline`, for example `rgb()` and `icon_time()`) are in the '
              'headers. They need no ABI name.')
    md.append('')

    # the contents
    groups = []
    for title, hs in GROUPS:
        names = [n for n in syms if n in found and found[n][0] in hs]
        if names:
            groups.append((title, hs))
    extra = sorted(set(found[n][0] for n in found if found[n][0] not in HEADER_TITLE))
    ext_groups = []
    for title, _ in EXTERN_RULES:
        if any(rule_of(n) == title for n in other):
            ext_groups.append(title)
    md.append('## Contents')
    md.append('')
    for title, hs in groups:
        md.append('- [%s](#%s) (%s)' % (title, anchor(title), ', '.join('`%s`' % h for h in hs if any(found[n][0] == h for n in found))))
    for h in extra:
        md.append('- [Other: %s](#%s)' % (h, anchor('Other: ' + h)))
    if raw:
        md.append('- [Raw ESP-IDF: the extra names](#%s)' % anchor('Raw ESP-IDF: the extra names'))
    for title in ext_groups:
        md.append('- [%s](#%s)' % (title, anchor(title)))
    md.append('- [Types and constants](#types-and-constants)')
    md.append('- [Header helpers](#header-helpers)')
    md.append('')

    def kinds_note(n):
        ks = syms[n]
        return '' if set(ks) >= set(KINDS) else ' *(only: %s)*' % ', '.join(ks)

    def emit_header_group(title, hs):
        md.append('## %s' % title)
        md.append('')
        for h in hs:
            sts = [st for st in parsed.get(h, []) if any(n in syms and found.get(n, (None,))[0] == h for n in st['names'])]
            if not sts:
                continue
            md.append('### `%s`' % h)
            md.append('')
            last_comment = None
            for st in sts:
                ns = sorted(n for n in st['names'] if n in syms and found[n][0] == h)
                md.append('#### %s%s' % (', '.join('`%s`' % n for n in ns), ''.join(kinds_note(n) for n in ns[:1])))
                md.append('')
                md.append('```c')
                md.append(decl_text(st))
                md.append('```')
                md.append('')
                c = ' '.join(st['comment']).strip()
                t = ' '.join(st['trail']).strip()
                if c and c == last_comment:
                    md.append('(The comment of the declarations above applies.)')
                    md.append('')
                elif c:
                    md.append(c)
                    md.append('')
                if t:
                    md.append(t)
                    md.append('')
                if not c and not t:
                    md.append('(No comment in the header.)')
                    md.append('')
                last_comment = c or last_comment
        if title == 'Touch input':
            pass

    for title, hs in groups:
        emit_header_group(title, hs)
    for h in extra:
        emit_header_group('Other: ' + h, [h])

    if raw:
        md.append('## Raw ESP-IDF: the extra names')
        md.append('')
        md.append('ESP-IDF functions that no built-in app calls. They come from `main/tfn_abi_extra.json` (see '
                  '[raw.md](raw.md)). Their declarations are in the ESP-IDF headers (for example `esp_wifi.h`). A name '
                  'with a permission class links only for an app whose manifest has the class: `mk_app.py` writes it, '
                  'and the STORE shows its text before the install. An app that calls a name of revision N runs only on '
                  'a firmware with ABI rev N or newer.')
        md.append('')
        perms = abi.get('perms', {})
        md.append('| Name | Permission class | Rev | Why |')
        md.append('| --- | --- | --- | --- |')
        for n in raw:
            p = abi.get('need', {}).get(n, '')
            ptxt = '`%s` (%s)' % (p, perms[p]['text']) if p in perms else 'none'
            md.append('| `%s`%s | %s | %d | %s |' % (n, kinds_note(n), ptxt, abi.get('revs', {}).get(n, 0),
                                                   xnames[n].get('why', '')))
        md.append('')
    for title in ext_groups:
        ns = [n for n in other if rule_of(n) == title]
        md.append('## %s' % title)
        md.append('')
        md.append(EXT_TEXT.get(title, 'The standard functions. No header of `main/` declares them.'))
        md.append('')
        for n in ns:
            note = kinds_note(n)
            if n in TRACKED:
                note += ': ' + TRACKED[n]
            md.append('- `%s`%s' % (n, note))
        md.append('')

    md.append('## Types and constants')
    md.append('')
    md.append('The types and the constants of the main headers, with their field comments. They are in the headers: '
              'they need no ABI name.')
    md.append('')
    for h in TYPE_HEADERS:
        sts = [st for st in parsed.get(h, []) if re.match(r'\s*(typedef|enum)\b', st['joined'])
               and not SKIP_TYPE.search(st['joined'])]
        ds = [d for d in defs.get(h, []) if not d['macro'] and not SKIP_TYPE.match(d['name'])]
        if not sts and not ds:
            continue
        md.append('### `%s`' % h)
        md.append('')
        for st in sts:
            c = ' '.join(st['comment']).strip()
            if c:
                md.append(c)
                md.append('')
            md.append('```c')
            md.append('\n'.join(t for t in st['text'] if t.strip()))
            md.append('```')
            md.append('')
        if ds:
            md.append('```c')
            for d in ds:
                md.append(d['text'] + ('    // ' + d['trail'] if d['trail'] else ''))
            md.append('```')
            md.append('')
    md.append('## Header helpers')
    md.append('')
    md.append('These functions are `static inline` in the headers. An app can use them with no ABI name.')
    md.append('')
    for h in HELPER_HEADERS:
        sts = [st for st in parsed.get(h, []) if st['inline']]
        if not sts:
            continue
        md.append('### `%s`' % h)
        md.append('')
        for st in sts:
            md.append('```c')
            md.append(decl_text(st))
            md.append('```')
            md.append('')
            c = ' '.join(st['comment']).strip() if st['own'] else ''
            c = c or HELPER_NOTES.get(next(iter(st['names']), ''), '')
            if c:
                md.append(c)
                md.append('')
    text = '\n'.join(md).rstrip() + '\n'

    if args.check:
        old = open(OUT, encoding='utf-8').read() if os.path.exists(OUT) else ''
        if old != text:
            print('sdk/docs/api.md is not current: run python sdk/tools/gen_api_doc.py')
            sys.exit(1)
        print('sdk/docs/api.md is current')
        return
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    open(OUT, 'w', encoding='utf-8', newline='\n').write(text)
    print('wrote %s: ABI %d rev %d, %d names (%d in headers of main/, %d standard names, %d raw ESP-IDF names)' %
          (os.path.relpath(OUT, ROOT), abi['version'], abi.get('rev', 0), len(syms), len(found), len(other), len(raw)))
    nocomment = sorted(n for n, (h, st) in found.items() if not st['comment'] and not st['trail'])
    if nocomment:
        print('names with no comment in the header (%d): %s' % (len(nocomment), ' '.join(nocomment)))


EXT_TEXT = {
    'Other firmware names': 'Firmware data that no header of `main/` declares by name. `app_menu` is the `app_t` of the '
                            'menu (`app.h` declares each built-in app with the `ARCADE_APP` list): `app_open(&app_menu)` '
                            'goes back to the menu. The `SCN_*` names are the scenes of SCENES (`scn_def_t`, `libs/scn/scn.h`; '
                            'each is defined in its `main/scn_<key>.c`).',
    'C library: other': 'Other functions of the C library: `qsort` (a sort) and `time` (the clock in seconds; it is '
                        'correct only after the board got the time from the network, see `esp_sntp_*`).',
    'Compiler runtime (automatic)': 'The compiler calls these for `double` and 64-bit arithmetic. Do not call them '
                                    'yourself. Use `float` where you can: the ESP32-S3 has a hardware `float` unit '
                                    'but no `double` unit.',
    'FreeRTOS': 'The task and queue functions of FreeRTOS. A task of an app must end before the app closes '
                '(see `behave.md`, "Tasks").',
    'ESP-IDF': 'The ESP-IDF functions that apps can call (time, log, random numbers, memory, the web server of the '
               'board, the JPEG decoder and the inflate of the ROM).',
    'C library: memory': 'The memory functions. For an app file, the system notes each block and frees it when the '
                         'app closes.',
    'C library: files': 'The file functions. The board storage is at `/data`, the SD card at `/sdcard`. For an app '
                        'file, the system closes the files that are open when the app closes.',
    'C library: text': 'The string, memory and number-text functions of the C library.',
    'C library: math': 'The math functions of the C library. Use the `float` versions (`sinf`, `sqrtf`) and '
                       '`fsin()` / `fcos()` of `gfx.h` for graphics.',
}


def rule_of(n):
    for title, f in EXTERN_RULES:
        if f(n):
            return title
    return None


def anchor(title):
    a = title.lower()
    a = re.sub(r'[^a-z0-9 _-]', '', a)
    return a.replace(' ', '-')


if __name__ == '__main__':
    main()
