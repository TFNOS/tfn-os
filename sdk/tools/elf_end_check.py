# Find the app files that hold an address one past the end of a loaded section.
# The loader of TFN OS (components/elf_loader esp_elf_map_sym) maps an address only when it is inside .text, .data,
# .rodata, .data.rel.ro or .bss. A loop end such as `s_blob + N` that is the end of .bss (the symbol _end) becomes 0,
# and the loop runs through all the memory (the board stops). A newer firmware maps the end address;
# app.ld v4 of mk_app.py puts 16 free bytes after each section, so an app file also works on an older firmware.
#   python elf_end_check.py <app.elf or a folder> ...      (a folder: all app.elf files under it)
import os
import struct
import sys

PLACED = ('.text', '.data', '.rodata', '.data.rel.ro', '.bss')


def check(path):
    d = open(path, 'rb').read()
    if d[:4] != b'\x7fELF':
        return None
    shoff, = struct.unpack_from('<I', d, 32)
    shnum, shstr = struct.unpack_from('<HH', d, 48)
    sh = [struct.unpack_from('<10I', d, shoff + 40 * i) for i in range(shnum)]
    names_at = sh[shstr][4]

    def nm(s):
        e = d.index(b'\0', names_at + s[0])
        return d[names_at + s[0]:e].decode()
    secs = [(nm(s), s[3], s[5]) for s in sh if nm(s) in PLACED and s[5]]
    phoff, = struct.unpack_from('<I', d, 28)
    phnum, = struct.unpack_from('<H', d, 44)
    # file offset of a vaddr (the LOAD segments)
    segs = [struct.unpack_from('<8I', d, phoff + 32 * i) for i in range(phnum)]

    def word_at(va):
        for p in segs:
            if p[0] == 1 and p[2] <= va < p[2] + p[4]:
                return struct.unpack_from('<I', d, p[1] + va - p[2])[0]
        return None
    bad = []
    for s in sh:
        if s[1] != 4:           # SHT_RELA
            continue
        for off in range(s[4], s[4] + s[5], 12):
            o, info, add = struct.unpack_from('<IIi', d, off)
            t = info & 0xff
            if t != 5:          # R_XTENSA_RELATIVE: the value is in the word (the addend is 0 here)
                continue
            v = (add & 0xffffffff) or word_at(o)
            if v is None or any(a <= v < a + n for _, a, n in secs):
                continue
            ends = [n for n, a, sz in secs if v == a + sz]
            if ends:
                bad.append('0x%x -> 0x%x (the end of %s)' % (o, v, ends[0]))
    return bad


def main():
    files = []
    for a in sys.argv[1:]:
        if os.path.isdir(a):
            for root, _, fs in os.walk(a):
                files += [os.path.join(root, f) for f in fs if f == 'app.elf']
        else:
            files.append(a)
    n_bad = 0
    for f in sorted(files):
        r = check(f)
        if r:
            n_bad += 1
            print('%s: %d end address%s: %s' % (f, len(r), 'es' if len(r) > 1 else '', '; '.join(r[:3])))
    print('%d of %d app files hold an end address' % (n_bad, len(files)))
    sys.exit(1 if n_bad else 0)


if __name__ == '__main__':
    main()
