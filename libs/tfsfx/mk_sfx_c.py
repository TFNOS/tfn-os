# libs/tfsfx: put the MP3 effects of an app into its app file, for tfsfx_load_mem: a board with no SD card
# has the effects too. The files <dir>/<name>.mp3 in the order of the names (the FXN list of the app) -> <out>.c and
# <out>.h with <P>_SFX_N, <P>_SFX_DATA[], <P>_SFX_LEN[]. A missing file: a 0 length (that effect is silent).
#   python libs/tfsfx/mk_sfx_c.py <sfx folder> <out path without .c> <PREFIX> name1 name2 ...
import os
import sys

src, out, P, names = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4:]
base = os.path.basename(out)
note = '// the sound effects of the app (made by libs/tfsfx/mk_sfx_c.py from %s; do not edit)' % os.path.basename(os.path.normpath(src))
h = [note, '#pragma once', '#include <stdint.h>', '#define %s_SFX_N %d' % (P, len(names)),
     'extern const uint8_t *const %s_SFX_DATA[%s_SFX_N];' % (P, P), 'extern const uint32_t %s_SFX_LEN[%s_SFX_N];' % (P, P), '']
c = [note, '#include "%s.h"' % base, '']
total, refs, lens = 0, [], []
for i, n in enumerate(names):
    f = os.path.join(src, n + '.mp3')
    if not os.path.exists(f):
        print('MISSING', f)
        refs.append('0')
        lens.append('0')
        continue
    b = open(f, 'rb').read()
    total += len(b)
    c.append('static const uint8_t %s_SFX_%d[%d] = {' % (P, i, len(b)))
    for k in range(0, len(b), 24):
        c.append('    ' + ', '.join('%d' % x for x in b[k:k + 24]) + ',')
    c.append('};')
    refs.append('%s_SFX_%d' % (P, i))
    lens.append('sizeof %s_SFX_%d' % (P, i))
c.append('const uint8_t *const %s_SFX_DATA[%s_SFX_N] = {%s};' % (P, P, ', '.join(refs)))
c.append('const uint32_t %s_SFX_LEN[%s_SFX_N] = {%s};' % (P, P, ', '.join(lens)))
open(out + '.h', 'w', newline='\n').write('\n'.join(h))
open(out + '.c', 'w', newline='\n').write('\n'.join(c) + '\n')
print('%s.c: %d effects, %d bytes' % (base, len(names), total))
