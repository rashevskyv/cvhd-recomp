#!/usr/bin/env python3
"""Which generated functions read a non-volatile register (r14-r31, f14-f31, v14-v31, v64-v127) before they
write it? With `non_volatile_as_local` those registers become zero-initialised C++ locals (the generator declares
`PPCRegister r14{};` and elides the __savegprlr/__restgprlr helpers), so such a read sees 0 instead of the caller's
value. The PPC ABI makes the registers callee-saved: a callee may only read them after writing them, except for the
prologue save to the stack (`std rN, x(r1)` = REX_STORE_U64(r1 + d, rN)), which the audit ignores, and the epilogue
restore, which is a write. Everything else is a candidate for the manifest's `share_registers` (or a hook).

    python3 tools/nonvolatile_audit.py generated/default [generated/dll_soma ...]

Textual order inside a function approximates execution order (a backward branch to a later write is a false
alarm; the output names the line for a look). Prints: function, register, the first reading line.
"""
import os
import re
import sys

FUNC_RE = re.compile(r'^DEFINE_REX_FUNC\((\w+)\)')
REG_RE = re.compile(r'\bctx\.(r(?:1[4-9]|2\d|3[01])|f(?:1[4-9]|2\d|3[01])|v(?:1[4-9]|2\d|3[01]|6[4-9]|[7-9]\d|1[01]\d|12[0-7]))\b')
# A write: `ctx.rN.u64 = `, `ctx.vN = vTemp`, a partial lane write `ctx.vN.s32[0] = ` (counted as a write: the other
# lanes are the callee's own business under the ABI too), and the SIMD stores into the register.
WRITE_RE = re.compile(r'^\s*ctx\.(r\d+|f\d+|v\d+)(?:\.[a-z]+\d*(?:\[\d+\])?)? = ')
VWRITE_RE = re.compile(r'^\s*simde_mm_store(?:_si128\(\(simde__m128i\*\)|_ps\(|_pd\()ctx\.(v\d+)\.')
# A vector save to the stack: `ea = ctx.r1.u32 + d;` then a store through REX_RAW_STORE_ADDR(ea) of ctx.vN.
EA_R1_RE = re.compile(r'^\s*ea = \(?ctx\.r1\.u32')  # also `ea = (ctx.r1.u32 + ctx.r12.u32) & ~0xF` (stvx vN, r1, r12)
VSAVE_RE = re.compile(r'^\s*(?:simde_mm_store_si128\(\(simde__m128i\*\)REX_RAW(?:_STORE)?_ADDR\(ea\), .*ctx\.(v\d+)\.|REX_STORE_U8\(ea \+ i, ctx\.(v\d+)\.)')
SAVE_RE = re.compile(r'^\s*REX_STORE_(?:U64|U32)(?:_ADDR)?\(ctx\.r1\.u32 \+ -?\d+, ctx\.(r\d+)\.(?:u64|u32)\)')
FSAVE_RE = re.compile(r'^\s*REX_STORE_(?:U64|F64)\(ctx\.r1\.u32 \+ -?\d+, ctx\.(f\d+)\.')


def audit(path):
    found = []
    with open(path, encoding='utf-8', errors='replace') as fp:
        fn = None
        written = set()
        flagged = set()
        ea_from_r1 = False
        for ln, line in enumerate(fp, 1):
            m = FUNC_RE.match(line)
            if m:
                fn = m.group(1)
                written = set()
                flagged = set()
                continue
            if fn is None or '__savegprlr' in fn or '__restgprlr' in fn or '__savefpr' in fn or '__restfpr' in fn \
                    or '__savevmx' in fn or '__restvmx' in fn:
                continue
            w = WRITE_RE.match(line) or VWRITE_RE.match(line)
            if w:
                written.add(w.group(1))
                continue
            s = SAVE_RE.match(line) or FSAVE_RE.match(line)
            if s:
                continue  # the prologue save of the caller's value: ABI-legal
            if EA_R1_RE.match(line):
                ea_from_r1 = True
                continue
            vs = VSAVE_RE.match(line)
            if vs and ea_from_r1:
                continue  # stvx / stvlx vN to the stack frame
            if line.lstrip().startswith('ea = '):
                ea_from_r1 = False
            for r in REG_RE.findall(line):
                if r not in written and r not in flagged:
                    flagged.add(r)
                    found.append((fn, r, ln, line.strip()[:100]))
    return found


def main():
    dirs = sys.argv[1:] or ['generated/default']
    total = 0
    for d in dirs:
        for name in sorted(os.listdir(d)):
            if not name.endswith('.cpp'):
                continue
            for fn, r, ln, text in audit(os.path.join(d, name)):
                print(f'{name}:{ln} {fn} reads {r} first: {text}')
                total += 1
    print(f'{total} read-before-write candidates')


if __name__ == '__main__':
    main()
