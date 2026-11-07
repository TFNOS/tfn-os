#!/usr/bin/env python3
"""The checks of same_code.py: the text mode (which changes are comment-only and which change the code) and the
assembly parser of the funcs mode (which differences of label numbers do not count)."""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from same_code import asm_functions, normalize, parse_renames, rename_funcs  # noqa: E402

# (old, new, the same code?)
CASES = [
    ('int a; // a note\n', 'int a;\n', True),
    ('/* two\n   lines */\nint a;\n', 'int a;\n', True),
    ('int a;\n\n\n// x\nint b;\n', 'int a;\nint b;\n', True),
    ('x  =  1;\r\n', 'x = 1;\n', True),
    ('s = "// not a comment";\n', 's = "";\n', False),
    ('s = "/* no */";\n', 's = "";\n', False),
    ("c = '\"'; // q\n", "c = '\"';\n", True),
    ('s = "a\\"b"; /* z */\n', 's = "a\\"b";\n', True),
    ('a/**/b\n', 'ab\n', False),
    ('#define X 1 // one\n', '#define X 1\n', True),
    ('// line \\\nint hidden;\nint b;\n', 'int b;\n', True),
    ('if (a) f();\n', 'if (a) {\nf();\n}\n', False),
    ('#define Y 2\nint y;\n', '#define Y 2 int y;\n', False),
    ('x = 1;\n', 'x = 2;\n', False),
]

PY_CASES = [
    ('x = 1  # a note\n', 'x = 1\n', True),
    ('# head (2026)\nx = 1\n', 'x = 1\n', True),
    ('s = "# not a comment"\n', 's = ""\n', False),
    ('x = 1\n', 'x = 2\n', False),
]



def asm(body, data='', name='f'):
    """A small assembly file of GCC with one function."""
    return ('%s\t.section\t.text.%s,"ax",@progbits\n\t.align\t4\n\t.type\t%s, @function\n%s:\n%s\t.size\t%s, .-%s\n'
            '\t.ident\t"GCC"\n' % (data, name, name, name, body, name, name))


XT_STR = '\t.section\t.rodata.str1.1\n.LC%d:\n\t.string\t"%s"\n\t.literal_position\n\t.literal .LC%d, .LC%d\n'
RV_STR = '\t.section\t.rodata.str1.4\n.LC%d:\n\t.string\t"%s"\n'

# (old assembly, new assembly, the same function?) of the funcs mode
ASM_CASES = [
    # other label numbers, the same order: the same
    (asm('\tbeqz\ta2, .L3\n\tj\t.L4\n.L3:\n\tretw.n\n.L4:\n'), asm('\tbeqz\ta2, .L9\n\tj\t.L7\n.L9:\n\tretw.n\n.L7:\n'),
     True),
    # the order of two blocks changes: different
    (asm('\tbeqz\ta2, .L3\n\tj\t.L4\n.L3:\n\tretw.n\n.L4:\n'), asm('\tbeqz\ta2, .L4\n\tj\t.L3\n.L3:\n\tretw.n\n.L4:\n'),
     False),
    # Xtensa: other literal and string numbers, the same text: the same
    (asm('\tl32r\ta10, .LC2\n', XT_STR % (1, 'board', 2, 1)), asm('\tl32r\ta10, .LC8\n', XT_STR % (5, 'board', 8, 5)),
     True),
    # Xtensa: an other string: different
    (asm('\tl32r\ta10, .LC2\n', XT_STR % (1, 'board', 2, 1)), asm('\tl32r\ta10, .LC2\n', XT_STR % (1, 'power', 2, 1)),
     False),
    # RISC-V: a string label with an other number: the same; an other string: different
    (asm('\tlui\ta4,%hi(.LC3)\n', RV_STR % (3, 'sd')), asm('\tlui\ta4,%hi(.LC7)\n', RV_STR % (7, 'sd')), True),
    (asm('\tlui\ta4,%hi(.LC3)\n', RV_STR % (3, 'sd')), asm('\tlui\ta4,%hi(.LC3)\n', RV_STR % (3, 'SD')), False),
    # the Xtensa loop end label with other numbers: the same
    (asm('\tloop\ta8, .L5_LEND\n.L5_LEND:\n'), asm('\tloop\ta8, .L2_LEND\n.L2_LEND:\n'), True),
    # the number of a static variable (s_ts.0 / s_ts.3): the same; an other variable: different
    (asm('\tl32r\ta8, s_ts.0\n'), asm('\tl32r\ta8, s_ts.3\n'), True),
    (asm('\tl32r\ta8, s_ts.0\n'), asm('\tl32r\ta8, s_tx.0\n'), False),
    # the Xtensa number of __func__ (__func__$16 / __func__$17): the same
    (asm('\tl32r\ta9, __func__$16\n'), asm('\tl32r\ta9, __func__$17\n'), True),
    # an other constant: different
    (asm('\tmovi\ta10, 20\n'), asm('\tmovi\ta10, 21\n'), False),
]

# (old assembly, new assembly, the same function?, with the rename fdiv=tz_fdiv?)
REN_CASES = [
    (asm('\tcall8\tfdiv\n'), asm('\tcall8\ttz_fdiv\n'), True, True),
    (asm('\tcall8\tfdiv\n'), asm('\tcall8\ttz_fdiv\n'), False, False),
    (asm('\tcall8\tfdiv2\n'), asm('\tcall8\ttz_fdiv2\n'), False, True),
    # the RISC-V instruction fdiv.s is not the function fdiv; a numbered copy fdiv.0 is
    (asm('\tfdiv.s\tfa5,fa4,fa5\n'), asm('\tfdiv.s\tfa5,fa4,fa5\n'), True, True),
    (asm('\tcall\tfdiv.0\n'), asm('\tcall\ttz_fdiv.0\n'), True, True),
]


def main():
    bad = 0
    cases = [(o, n, s, 'x.c') for o, n, s in CASES] + [(o, n, s, 'x.py') for o, n, s in PY_CASES]
    for k, (old, new, same, path) in enumerate(cases):
        got = normalize(old, path) == normalize(new, path)
        if got != same:
            bad += 1
            print('FAIL case %d: %r -> %r: same %s, expected %s' % (k, old, new, got, same))
    for k, (old, new, same) in enumerate(ASM_CASES):
        a, b = asm_functions(old), asm_functions(new)
        got = 'f' in a and a == b
        if got != same:
            bad += 1
            print('FAIL asm case %d: same %s, expected %s\n  %r\n  %r' % (k, got, same, a.get('f'), b.get('f')))
    # --rename: a call of a renamed function is the same; without the rename it is different; a longer name stays
    ren = {'fdiv': 'tz_fdiv'}
    for k, (old, new, same, rn) in enumerate(REN_CASES):
        a = rename_funcs(asm_functions(old), ren if rn else {})
        b = asm_functions(new)
        got = 'f' in a and a == b
        if got != same:
            bad += 1
            print('FAIL rename case %d: same %s, expected %s\n  %r\n  %r' % (k, got, same, a.get('f'), b.get('f')))
    if parse_renames('a=b, c=d') != {'a': 'b', 'c': 'd'}:
        bad += 1
        print('FAIL parse_renames')
    cases += ASM_CASES + REN_CASES
    print('SAME CODE TESTS %s (%d cases)' % ('OK' if not bad else 'FAILED', len(cases)))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
