#!/usr/bin/env python3
"""Read-before-write of the LOCALIZED registers in generated code (non_volatile_as_local on): a function declares
`PPCRegister r14{};` (zero) and must write r14 before it reads it. The ctx-based audit cannot see these functions
(their registers are bare names). Prologue saves to the stack (`REX_STORE_U64(ctx.r1.u32 + d, rN.u64)`) and vector
saves through `ea` from r1 are ignored like in tools/nonvolatile_audit.py."""
import os
import re
import sys

FUNC_RE = re.compile(r'^DEFINE_REX_FUNC\((\w+)\)')
DECL_RE = re.compile(r'^\s*PPC(?:V)?Register ([rfv]\d+)\{\};')
WRITE_RE = re.compile(r'^\s*([rfv]\d+)(?:\.[a-z]+\d*(?:\[\d+\])?)? = ')
VWRITE_RE = re.compile(r'^\s*simde_mm_store(?:_si128\(\(simde__m128i\*\)|_ps\(|_pd\()([rfv]\d+)\.')
SAVE_RE = re.compile(r'^\s*REX_STORE_(?:U64|U32|F64)(?:_ADDR)?\(ctx\.r(?:1|12)\.u32 \+ -?\d+, ([rf]\d+)\.')  # frame via r1 or r12
EA_R1_RE = re.compile(r'^\s*ea = \(?ctx\.r1[12]?\.u32')  # r1, r11 or r12 based (stvx vN,r11,r12 in the save helpers)
VSAVE_RE = re.compile(r'^\s*(?:simde_mm_store_si128\(\(simde__m128i\*\)REX_RAW(?:_STORE)?_ADDR\(ea\), .*\b(v\d+)\.|REX_STORE_U8\(ea \+ i, (v\d+)\.)')
READ_RE = re.compile(r'(?<![\w.])([rfv]\d+)\b')


def audit(path):
    found = []
    with open(path, encoding='utf-8', errors='replace') as fp:
        fn = None
        locals_ = set()
        written = set()
        flagged = set()
        ea_from_r1 = False
        for ln, line in enumerate(fp, 1):
            m = FUNC_RE.match(line)
            if m:
                fn, locals_, written, flagged, ea_from_r1 = m.group(1), set(), set(), set(), False
                continue
            if fn is None or fn.startswith('__save') or fn.startswith('__rest'):
                continue  # the save/restore helper bodies: calls to them are elided
            d = DECL_RE.match(line)
            if d:
                locals_.add(d.group(1))
                continue
            if not locals_:
                continue
            if line.lstrip().startswith('//'):
                continue
            w = WRITE_RE.match(line) or VWRITE_RE.match(line)
            if w:
                rhs = line.split(' = ', 1)[1] if ' = ' in line else (line.split(',', 1)[1] if ',' in line else '')
                for r in READ_RE.findall(rhs):
                    if r in locals_ and r not in written and r not in flagged:
                        flagged.add(r)
                        found.append((fn, r, ln, line.strip()[:110]))
                written.add(w.group(1))
                continue
            if SAVE_RE.match(line):
                continue
            if EA_R1_RE.match(line):
                ea_from_r1 = True
                continue
            vs = VSAVE_RE.match(line)
            if vs and ea_from_r1:
                continue
            if line.lstrip().startswith('ea = '):
                ea_from_r1 = False
            for r in READ_RE.findall(line):
                if r in locals_ and r not in written and r not in flagged:
                    flagged.add(r)
                    found.append((fn, r, ln, line.strip()[:110]))
    return found


def main():
    total = 0
    for d in sys.argv[1:] or ['generated/default']:
        for name in sorted(os.listdir(d)):
            if name.endswith('.cpp'):
                for fn, r, ln, text in audit(os.path.join(d, name)):
                    print(f'{name}:{ln} {fn} reads local {r} first: {text}')
                    total += 1
    print(f'{total} local read-before-write candidates')


if __name__ == '__main__':
    main()
