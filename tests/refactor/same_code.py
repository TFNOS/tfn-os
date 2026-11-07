#!/usr/bin/env python3
"""The proof that a clean-up commit does not change the code.

Two modes:

  text     For each changed C file (.c, .h, .inc) or Python file the comments become one space, the white space folds, and the
           result must be the same as in the base version. This proves a change of the comments only. Pure Python,
           no compiler: CI runs it.

  objects  Compiles the base version and the new version of each changed .c file, and of each .c file that
           includes a changed header, with the real compile command of each board kind (build_dev/<kind>/
           compile_commands.json of a DEV build). __LINE__ is 0 and there is no debug data, so a line move does
           not count. The stripped object files must be the same, byte for byte. This proves a format change
           (white space, line breaks, braces round one statement).

  funcs    Compiles the given .c files in the base version and in the new version to assembly text (gcc -S), and
           compares each function by its name. The local labels get new numbers in the order of use, and each
           string or constant label becomes its content. A function may move to an other file. This proves a move
           of functions to a shared file. Run it two times:
             --cflags=-O0   THE PROOF. GCC compiles each function alone, so a difference is a difference of the
                            source of that function after the preprocessor (a macro, a type, a constant).
             (no --cflags)  THE REPORT of the real code. GCC also makes choices for the whole file (inline, the
                            order of the blocks, constant clones), so a function that did not change can show a
                            difference when its file loses or gets functions. Read each one.
           The object code is not compared here: the Xtensa assembler can write other bytes (padding, short or long
           branches) for the same function text when other functions of the file change.

Usage:
  python tests/refactor/same_code.py text    [--base REF] [files...]
  python tests/refactor/same_code.py objects [--base REF] [--kinds lcd349,cores3] [--build-dir DIR]
                                              [--extra-root DIR] [--all] [--jobs N] [files...]
  python tests/refactor/same_code.py funcs files... [--base REF] [--cflags=-O0] [--allow f,g]
                                            [--rename old=new,...] [--verbose] [...]

--base        the git ref of the old version (default: origin/main).
--build-dir   the folder with build_dev/<kind>/compile_commands.json (default: build_dev of this repo).
--extra-root  a folder with the same layout as the repo that has the files which git does not track (private
              headers): the compiler looks there after the version folder.
--all         compile all .c files of main/, libs/, third_party/, apps/ and components/, not only the changed ones.
--rename      funcs: the names that the change gives a new name (old=new, with commas between them). The tool
              gives the new names to the base version before the compare: a function that only calls a renamed
              function is the same. A file whose new version still has a function of the old name keeps it.

A file that the change moves (objects: git sees a rename with the same content, for example libs/zstd/ ->
third_party/zstd/): the base version compiles the old path, the new version the new path, both with the compile
command of the old path.

Exit 0: the same code. Exit 1: a difference, or a file that compiles in one version only.
"""
import argparse
import concurrent.futures
import io
import json
import os
import re
import shlex
import subprocess
import sys
import tarfile
import tokenize

ROOT = subprocess.run(['git', 'rev-parse', '--show-toplevel'], capture_output=True, text=True,
                      cwd=os.path.dirname(os.path.abspath(__file__))).stdout.strip()
SRC_DIRS = ['main', 'boards', 'libs', 'third_party', 'apps', 'components']
C_EXT = ('.c', '.h', '.inc', '.cc', '.cpp', '.hpp')
UNIT_EXT = ('.c', '.cc', '.cpp')
TEXT_EXT = ('.py', '.cmake', '.projbuild', '.sh')
TEXT_NAMES = ('CMakeLists.txt', 'Kconfig')
WORK_MARK = '/build_dev/work'
TEMPLATE = 'main/app_settings.c'
INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.M)
WORK_DIR = os.path.join(ROOT, 'tests', 'host', 'same_code')
EXTRA_CFLAGS = []  # --cflags


def git(*args, text=True):
    r = subprocess.run(['git'] + list(args), cwd=ROOT, capture_output=True, text=text)
    if r.returncode:
        sys.exit('git %s: %s' % (' '.join(args), r.stderr if text else r.stderr.decode()))
    return r.stdout


def changed_files(base, text_too=False):
    out = git('diff', '--name-only', base, '--', *SRC_DIRS)
    ext = C_EXT + TEXT_EXT if text_too else C_EXT
    return [f for f in out.split() if f.endswith(ext) or (text_too and os.path.basename(f) in TEXT_NAMES)]


def base_text(base, path):
    r = subprocess.run(['git', 'show', '%s:%s' % (base, path)], cwd=ROOT, capture_output=True)
    if r.returncode:
        return None
    return r.stdout.decode('utf-8', 'replace')


# ---- the text mode ----

def strip_comments(src):
    """Each comment becomes one space (as in C). Strings and character constants stay as they are."""
    out = []
    i = 0
    n = len(src)
    while i < n:
        c = src[i]
        if c in '"\'':
            j = i + 1
            while j < n and src[j] != c:
                j += 2 if src[j] == '\\' else 1
            out.append(src[i:j + 1])
            i = j + 1
        elif src.startswith('//', i):
            j = i
            while j < n and src[j] != '\n':
                j += 2 if src[j] == '\\' else 1
            out.append(' ')
            i = j
        elif src.startswith('/*', i):
            j = src.find('*/', i + 2)
            i = n if j < 0 else j + 2
            out.append(' ')
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def normalize_py(src):
    """The tokens of a Python file with no comments (the tokenizer knows a comment from a string)."""
    toks = tokenize.generate_tokens(io.StringIO(src.replace('\r\n', '\n')).readline)
    skip = (tokenize.COMMENT, tokenize.NL)
    return '\n'.join('%d %s' % (t.type, t.string) for t in toks if t.type not in skip)


def strip_hash_comments(src):
    """CMake, Kconfig and shell: a '#' outside a quoted string starts a comment to the end of the line."""
    out = []
    for ln in src.split('\n'):
        quoted = False
        cut = len(ln)
        for k, c in enumerate(ln):
            if c == '"' and (k == 0 or ln[k - 1] != '\\'):
                quoted = not quoted
            elif c == '#' and not quoted:
                cut = k
                break
        out.append(ln[:cut])
    return '\n'.join(out)


def normalize(src, path=''):
    """The code with no comments, one space for each run of spaces, no empty lines."""
    if path.endswith('.py'):
        return normalize_py(src)
    if os.path.basename(path) in ('CMakeLists.txt', 'Kconfig') or path.endswith(('.cmake', '.projbuild', '.sh')):
        text = strip_hash_comments(src.replace('\r\n', '\n'))
    else:
        text = strip_comments(src.replace('\r\n', '\n'))
    lines = [re.sub(r'[ \t\f\v]+', ' ', ln).strip() for ln in text.split('\n')]
    return '\n'.join(ln for ln in lines if ln)


def first_difference(a, b):
    la = a.split('\n')
    lb = b.split('\n')
    for k in range(min(len(la), len(lb))):
        if la[k] != lb[k]:
            return 'code line %d: %r -> %r' % (k + 1, la[k][:80], lb[k][:80])
    return 'code lines %d -> %d' % (len(la), len(lb))


def run_text(args):
    files = args.files or changed_files(args.base, text_too=True)
    bad = 0
    for f in files:
        old = base_text(args.base, f)
        path = os.path.join(ROOT, f)
        if old is None or not os.path.exists(path):
            print('NEW OR GONE %s (not compared)' % f)
            continue
        with open(path, encoding='utf-8', errors='replace') as h:
            new = h.read()
        a = normalize(old, f)
        b = normalize(new, f)
        if a != b:
            bad += 1
            print('CODE CHANGED %s: %s' % (f, first_difference(a, b)))
    print('SAME CODE (text): %d files, %d with a code change' % (len(files), bad))
    return 1 if bad else 0


# ---- the objects mode ----

def export_base(base):
    sha = git('rev-parse', base).strip()
    tree = os.path.join(WORK_DIR, 'base-' + sha[:10])
    if os.path.isdir(os.path.join(tree, 'main')):
        return tree
    have = set(git('ls-tree', '--name-only', sha).split())
    data = git('archive', '--format=tar', sha, *[d for d in SRC_DIRS if d in have], text=False)
    with tarfile.open(fileobj=io.BytesIO(data)) as t:
        t.extractall(tree)
    return tree


def moves(base):
    """{new path: old path} of the files that the change moves (git sees a rename with the same content)."""
    out = {}
    for ln in git('diff', '--name-status', '-M100%', base, '--', *SRC_DIRS).splitlines():
        p = ln.split('\t')
        if len(p) == 3 and p[0].startswith('R'):
            out[p[2]] = p[1]
    return out


def all_sources(tree):
    out = []
    for d in SRC_DIRS:
        for dp, _, fs in os.walk(os.path.join(tree, d)):
            for f in fs:
                if f.endswith(C_EXT):
                    out.append(os.path.relpath(os.path.join(dp, f), tree).replace(os.sep, '/'))
    return out


def dependents(changed, tree):
    """The .c files that include a changed file, directly or through other headers (by the base name)."""
    srcs = all_sources(tree)
    incs = {}
    for s in srcs:
        with open(os.path.join(tree, s), encoding='utf-8', errors='replace') as h:
            incs[s] = {os.path.basename(x) for x in INCLUDE_RE.findall(h.read())}
    hit = {os.path.basename(c) for c in changed}
    found = set(c for c in changed if c.endswith(UNIT_EXT))
    grew = True
    while grew:
        grew = False
        for s, names in incs.items():
            if s not in found and names & hit:
                found.add(s)
                if not s.endswith(UNIT_EXT):
                    hit.add(os.path.basename(s))
                grew = True
    return sorted(f for f in found if f.endswith(UNIT_EXT))


def load_kind(build_dir, kind):
    path = os.path.join(build_dir, kind, 'compile_commands.json')
    with open(path) as h:
        db = json.load(h)
    entries = {}
    work = None
    for e in db:
        f = e['file'].replace('\\', '/')
        k = f.find(WORK_MARK + '/')
        if k >= 0:
            work = f[:k + len(WORK_MARK)]
            entries[f[k + len(WORK_MARK) + 1:]] = e
    return {'kind': kind, 'entries': entries, 'work': work}


def compile_args(kind, rel, tree, extra, cmd_rel=None):
    e = kind['entries'].get(cmd_rel or rel) or kind['entries'][TEMPLATE]
    # (the path backslashes become slashes; an escaped quote of a -D value stays)
    args = shlex.split(re.sub(r'\\(?!")', '/', e['command']))
    src_name = e['file'].replace('\\', '/')
    cc = args[0]
    out = []
    skip = False
    fallback = []
    for a in args[1:]:
        if skip:
            skip = False
            continue
        if a in ('-o', '-MF', '-MT', '-MQ'):
            skip = True
            continue
        if a in ('-c', '-MD', '-MMD') or a.startswith(('-g', '-Werror')) or a == src_name:
            continue
        if extra and a.startswith('-I' + kind['work']):
            fallback.append(a.replace(kind['work'], extra))
        out.append(a.replace(kind['work'], tree))
    # (libs/ itself: an app includes a lib in the SDK form "tfdraw/tfdraw.h"; a build made before main/CMakeLists.txt
    # had it does not give it. It changes no object of a tree whose files include the libs by their path.)
    libs = '-I' + tree + '/libs'
    if libs not in out:
        out.append(libs)
    # (third_party/: the folder and each of its folders, as main/apps_gen.py gives them to the build; before the
    # folders of --extra-root)
    tp = os.path.join(tree, 'third_party')
    if os.path.isdir(tp):
        out.append('-I' + tree + '/third_party')
        out += ['-I' + tree + '/third_party/' + d for d in sorted(os.listdir(tp)) if os.path.isdir(os.path.join(tp, d))]
    line0 = os.path.join(WORK_DIR, 'line0.h').replace('\\', '/')
    return cc, out + fallback + ['-g0', '-w', '-include', line0] + EXTRA_CFLAGS + ['-c', rel]


def build_one(job):
    kind, rel, tree, extra, tag = job[:5]
    cmd_rel = job[5] if len(job) > 5 else None
    cc, args = compile_args(kind, rel, tree, extra, cmd_rel)
    name = '%s_%s_%s' % (kind['kind'], tag, rel.replace('/', '__'))
    obj = os.path.join(WORK_DIR, 'obj', name + '.o').replace('\\', '/')
    rsp = os.path.join(WORK_DIR, 'obj', name + '.rsp')
    with open(rsp, 'w') as h:
        for a in args + ['-o', obj]:
            h.write('"%s"\n' % a.replace('"', '\\"'))
    env = dict(os.environ, SOURCE_DATE_EPOCH='0')
    r = subprocess.run([cc, '@' + rsp], cwd=tree, capture_output=True, text=True, env=env)
    if r.returncode:
        return None, (r.stdout + r.stderr)[-600:]
    stripped = obj[:-2] + '.s.o'
    objcopy = re.sub(r'(gcc|g\+\+)(\.exe)?$', r'objcopy\2', cc)
    r = subprocess.run([objcopy, '--strip-debug', '--remove-section=.comment', obj, stripped],
                       capture_output=True, text=True)
    if r.returncode:
        return None, r.stderr[-600:]
    with open(stripped, 'rb') as h:
        return h.read(), ''


def compare_kind(kind, files, base_tree, extra, jobs, moved=None):
    moved = moved or {}
    tasks = []
    for rel in files:
        old = moved.get(rel, rel)
        tasks.append((kind, old, base_tree, extra, 'base'))
        tasks.append((kind, rel, ROOT.replace('\\', '/'), extra, 'new', old))
    with concurrent.futures.ThreadPoolExecutor(jobs) as ex:
        res = list(ex.map(build_one, tasks))
    same, diff, both_fail, one_fail = [], [], [], []
    for k, rel in enumerate(files):
        (a, ea), (b, eb) = res[2 * k], res[2 * k + 1]
        if a is None and b is None:
            both_fail.append(rel)
        elif a is None or b is None:
            one_fail.append((rel, ea or eb))
        elif a == b:
            same.append(rel)
        else:
            diff.append(rel)
    return same, diff, both_fail, one_fail


NUM_SUFFIX = re.compile(r'\b([A-Za-z_]\w*)[.$]\d+\b')  # s_ts.0, __func__$16 -> .N (numbers of static data)
LOCAL_LABEL = re.compile(r'\.L(C?)(\d+)((?:_\w+)?)\b')  # (the Xtensa loop end: .L12_LEND)
FUNC_START = re.compile(r'^\s*\.type\s+([A-Za-z_][\w.]*),\s*[@%]function')
DATA_LABEL = re.compile(r'^(\.LC?\d+):\s*$')
XT_LITERAL = re.compile(r'^\s*\.literal\s+(\.LC?\d+),\s*(.*)$')
DATA_END = re.compile(r'^\s*\.(section|text|data|bss|align|p2align|balign|type|size|global|globl|local|weak|'
                      r'literal_position|literal|ident|file|option|attribute|previous|popsection|pushsection)\b|^\S+:')


def build_asm(job):
    """The assembly text (gcc -S) of one file. Returns (text, error)."""
    kind, rel, tree, extra, tag = job
    cc, args = compile_args(kind, rel, tree, extra)
    args = [a for a in args if a != '-c'] + ['-S']
    name = '%s_%s_%s' % (kind['kind'], tag, rel.replace('/', '__'))
    out = os.path.join(WORK_DIR, 'obj', name + '.s').replace('\\', '/')
    rsp = os.path.join(WORK_DIR, 'obj', name + '.s.rsp')
    with open(rsp, 'w') as h:
        for a in args + ['-o', out]:
            h.write('"%s"\n' % a.replace('"', '\\"'))
    env = dict(os.environ, SOURCE_DATE_EPOCH='0')
    r = subprocess.run([cc, '@' + rsp], cwd=tree, capture_output=True, text=True, env=env)
    if r.returncode:
        return None, (r.stdout + r.stderr)[-600:]
    with open(out, encoding='utf-8', errors='replace') as h:
        return h.read(), ''


def asm_functions(text):
    """{function name: [normalized lines]} of one assembly file. A local label (.L12) gets a number in the order
    of its first use in the function. A data label (.LC3: a string, a constant, a literal of Xtensa) becomes its
    content. So a move to an other file, with other label numbers, does not count."""
    lines = text.splitlines()
    data = {}
    k = 0
    while k < len(lines):
        m = XT_LITERAL.match(lines[k])
        if m:
            data[m.group(1)] = 'literal ' + m.group(2).strip()
            k += 1
            continue
        m = DATA_LABEL.match(lines[k])
        if m and k + 1 < len(lines) and not FUNC_START.match(lines[k + 1]):
            body = []
            j = k + 1
            while j < len(lines) and not DATA_END.match(lines[j]):
                body.append(lines[j].strip())
                j += 1
            if body:
                data[m.group(1)] = ' ; '.join(body)
        k += 1

    def content(label, depth=0):
        v = data.get(label)
        if v is None or depth > 4:
            return None
        return LOCAL_LABEL.sub(lambda m: content(m.group(0), depth + 1) or m.group(0), v)

    funcs = {}
    k = 0
    while k < len(lines):
        m = FUNC_START.match(lines[k])
        if not m:
            k += 1
            continue
        name = m.group(1)
        j = k + 1
        while j < len(lines) and lines[j].strip() != name + ':':
            j += 1
        body = []
        j += 1
        while j < len(lines) and not re.match(r'^\s*\.size\s+%s\b' % re.escape(name), lines[j]):
            body.append(lines[j].strip())
            j += 1
        numbers = {}

        def number(m):
            key = '.L' + m.group(1) + m.group(2)
            return '.L' + m.group(1) + str(numbers.setdefault(key, len(numbers))) + m.group(3)

        def label(m):
            if m.group(1):
                c = content(m.group(0))
                if c is not None:
                    # (a label that stays in the content, such as a code label of a jump table, gets a number too)
                    return '<' + LOCAL_LABEL.sub(number, c) + '>'
            return number(m)

        funcs[NUM_SUFFIX.sub(r'\1.N', name)] = [NUM_SUFFIX.sub(r'\1.N', LOCAL_LABEL.sub(label, x)) for x in body]
        k = j
    return funcs


def parse_renames(text):
    """{old: new} from "old=new,old2=new2"."""
    out = {}
    for part in (text or '').split(','):
        if part.strip():
            old, new = part.split('=')
            out[old.strip()] = new.strip()
    return out


def rename_funcs(funcs, renames):
    """The functions of one file with the new names (the names and each whole-word use in the lines)."""
    if not renames:
        return funcs
    # (not an instruction such as the RISC-V "fdiv.s": a name with a dot and a letter after it; but "name.N" is a
    # numbered copy of the function, after NUM_SUFFIX)
    word = re.compile(r'(?<![\w.$])(%s)(?![\w$]|\.(?!N\b)[A-Za-z])' % '|'.join(re.escape(k) for k in renames))
    return {renames.get(fn, fn): [word.sub(lambda m: renames[m.group(1)], x) for x in v] for fn, v in funcs.items()}


def run_funcs(args):
    """The proof of a move of functions: each function of the base version must have the same code in the new
    version, in any of the given files."""
    os.makedirs(os.path.join(WORK_DIR, 'obj'), exist_ok=True)
    with open(os.path.join(WORK_DIR, 'line0.h'), 'w') as h:
        h.write('#define __LINE__ 0\n')
    base_tree = export_base(args.base).replace('\\', '/')
    new_tree = ROOT.replace('\\', '/')
    build_dir = args.build_dir or os.path.join(ROOT, 'build_dev')
    kinds = args.kinds.split(',') if args.kinds else sorted(
        k for k in os.listdir(build_dir) if os.path.exists(os.path.join(build_dir, k, 'compile_commands.json')))
    extra = os.path.abspath(args.extra_root).replace('\\', '/') if args.extra_root else None
    files = [f for f in args.files if f.endswith(UNIT_EXT)]
    allowed = set(args.allow.split(',')) if args.allow else set()
    renames = parse_renames(args.rename)
    print('base %s, %d .c files, kinds %s' % (args.base, len(files), ','.join(kinds)))
    bad = 0
    for name in kinds:
        kind = load_kind(build_dir, name)
        tasks = []
        for rel in files:
            if os.path.exists(os.path.join(base_tree, rel)):
                tasks.append((kind, rel, base_tree, extra, 'base'))
            if os.path.exists(os.path.join(new_tree, rel)):
                tasks.append((kind, rel, new_tree, extra, 'new'))
        with concurrent.futures.ThreadPoolExecutor(args.jobs) as ex:
            res = list(ex.map(build_asm, tasks))
        side = {'base': {}, 'new': {}}
        fails = []
        parsed = []
        for t, (data, err) in zip(tasks, res):
            if data is None:
                fails.append((t[4], t[1], err))
                continue
            parsed.append((t, asm_functions(data)))
        # a file that still has its own function of an old name keeps that name (a static function of the same name)
        new_names = {}
        for t, funcs in parsed:
            if t[4] == 'new':
                new_names[t[1]] = set(funcs)
        for t, funcs in parsed:
            if t[4] == 'base':
                own = new_names.get(t[1], set())
                funcs = rename_funcs(funcs, {k: v for k, v in renames.items() if k not in own})
            for fn, v in funcs.items():
                side[t[4]].setdefault(fn, []).append((t[1], v))
        if len(fails) == len(tasks):
            # (nothing is proven for this kind: a fault; the first error shows the cause)
            err = fails[0][2].strip().splitlines()[-1] if fails and fails[0][2].strip() else ''
            print('%-8s no file builds for this kind: %s' % (name, err))
            bad += 1
            continue
        same, diff, gone, new = [], [], [], []
        for fn, olds in sorted(side['base'].items()):
            news = side['new'].get(fn)
            if not news:
                gone.append(fn)
            elif all(any(o[1] == n[1] for n in news) for o in olds):
                same.append(fn)
            else:
                diff.append((fn, olds[0][0], news[0][0]))
        new = sorted(fn for fn in side['new'] if fn not in side['base'])
        print('%-8s functions: same %d, different %d, gone %d, new %d' % (name, len(same), len(diff), len(gone),
                                                                          len(new)))
        for fn, a, b in diff:
            mark = 'ALLOWED' if fn in allowed else 'DIFFERENT'
            print('  %s %s (%s -> %s)' % (mark, fn, a, b))
            bad += fn not in allowed
            if args.verbose:
                x = side['base'][fn][0][1]
                y = side['new'][fn][0][1]
                k = next((i for i in range(min(len(x), len(y))) if x[i] != y[i]), min(len(x), len(y)))
                print('      line %d of %d/%d: %r -> %r' % (k, len(x), len(y), x[k] if k < len(x) else None,
                                                         y[k] if k < len(y) else None))
        for fn in gone:
            mark = 'ALLOWED' if fn in allowed else 'GONE'
            print('  %s %s' % (mark, fn))
            bad += fn not in allowed
        if new:
            print('  new: %s' % ', '.join(new))
        built = {(t[4], t[1]) for t, r in zip(tasks, res) if r[0] is not None}
        for tag, rel, err in fails:
            other = ('new' if tag == 'base' else 'base', rel)
            print('  NO BUILD (%s) %s: %s' % (tag, rel, err.strip().splitlines()[-1] if err.strip() else ''))
            # (a file that builds in neither version is not proven: a fault too, counted one time)
            bad += other in built or tag == 'new'
    print('SAME CODE (funcs): %s' % ('OK' if not bad else '%d FAULTS' % bad))
    return 1 if bad else 0


def run_objects(args):
    os.makedirs(os.path.join(WORK_DIR, 'obj'), exist_ok=True)
    with open(os.path.join(WORK_DIR, 'line0.h'), 'w') as h:
        h.write('#define __LINE__ 0\n')
    base_tree = export_base(args.base).replace('\\', '/')
    moved = moves(args.base)
    if args.all:
        files = sorted(f for f in all_sources(ROOT) if f.endswith(UNIT_EXT))
    else:
        # (the old path of a moved file is not in the new tree: the new path stands for it)
        gone = set(moved.values())
        files = [f for f in dependents(args.files or changed_files(args.base), ROOT) if f not in gone]
    if any(f in moved for f in files):
        print('moved: %s' % ', '.join('%s <- %s' % (f, moved[f]) for f in files if f in moved))
    build_dir = args.build_dir or os.path.join(ROOT, 'build_dev')
    kinds = args.kinds.split(',') if args.kinds else sorted(
        k for k in os.listdir(build_dir) if os.path.exists(os.path.join(build_dir, k, 'compile_commands.json')))
    extra = os.path.abspath(args.extra_root).replace('\\', '/') if args.extra_root else None
    print('base %s, %d .c files, kinds %s' % (args.base, len(files), ','.join(kinds)))
    bad = 0
    for name in kinds:
        same, diff, both_fail, one_fail = compare_kind(load_kind(build_dir, name), files, base_tree, extra, args.jobs,
                                                       moved)
        print('%-8s same %d, different %d, no build in both %d, build in one only %d'
              % (name, len(same), len(diff), len(both_fail), len(one_fail)))
        for f in diff:
            print('  DIFFERENT %s' % f)
        for f, err in one_fail:
            print('  ONE VERSION ONLY %s: %s' % (f, err.strip().splitlines()[-1] if err.strip() else ''))
        if args.verbose:
            for f in both_fail:
                print('  no build %s' % f)
        bad += len(diff) + len(one_fail)
    print('SAME CODE (objects): %s' % ('OK' if not bad else '%d FAULTS' % bad))
    return 1 if bad else 0


def main():
    p = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    p.add_argument('mode', choices=['text', 'objects', 'funcs'])
    p.add_argument('--allow', help='funcs: the names of functions that may change (an expected change)')
    p.add_argument('--rename', help='funcs: old=new,... the names that the change gives a new name')
    p.add_argument('files', nargs='*')
    p.add_argument('--base', default='origin/main')
    p.add_argument('--kinds')
    p.add_argument('--build-dir')
    p.add_argument('--extra-root')
    p.add_argument('--all', action='store_true')
    p.add_argument('--jobs', type=int, default=os.cpu_count() or 4)
    p.add_argument('--verbose', action='store_true')
    p.add_argument('--cflags', help='more compiler options for both versions (an analysis aid, not the proof)')
    args = p.parse_args()
    if args.cflags:
        EXTRA_CFLAGS.extend(shlex.split(args.cflags))
    if args.mode == 'text':
        return run_text(args)
    if args.mode == 'funcs':
        return run_funcs(args)
    return run_objects(args)


if __name__ == '__main__':
    sys.exit(main())
