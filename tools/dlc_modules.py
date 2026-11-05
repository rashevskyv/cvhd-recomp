#!/usr/bin/env python3
"""The DLC's code DLLs as recompiled modules.

    python3 -I tools/dlc_modules.py manifest <installed_dlc_dir|-> <game_dir> <manifest>
        Copies every *.dll of the installed packages (tools/dlc_install.py output:
        .../58410A7A/00000002/<package>/...) to <game_dir>/dlc/<path in the package> ("-": use the DLLs
        already there) and rewrites the block between the DLC markers of <manifest> with one [[modules]]
        entry per DLL, plus the extra function starts of tools/dlc_fixes.txt.
    python3 -I tools/dlc_modules.py learn <codegen_log>
        Adds the "target not in any function" addresses of a failed codegen run to tools/dlc_fixes.txt
        (the module named by "Analysis failed for '<stem>'"); then run "manifest -" and codegen again.
        guest_path is "dlc:\\<path>": the runtime matches a loaded XEX by its path without the
        device (NormalizeGuestPath), so DLC00:\\data\\player\\dllSimon.dll finds data/player/dllsimon.dll.
    python3 -I tools/dlc_modules.py tables <generated_dir>
        After codegen: reads REX_CODE_BASE / REX_CODE_SIZE of every generated/dlc/*/*_pch.h and writes
        <generated_dir>/rex_dll_tables.h: a function-table slot per code base, packed from 0x8C000000
        (above the highest DLC image, 0x8BF80000). The 0x81000000 + 2 * (code_base - 0x83000000) rule of
        the six character DLLs would put these tables past 0x90000000 (the folded XEX alias).
    python3 -I tools/dlc_modules.py --selftest
"""
import glob, os, re, shutil, struct, sys

BEGIN = '# --- DLC code modules (tools/dlc_modules.py manifest): do not edit by hand ---'
END = '# --- end of DLC code modules ---'
TABLE_START, TABLE_END = 0x8C000000, 0x90000000
THUNK_RESERVE = 0x10000
FIXES = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'dlc_fixes.txt')


def read_fixes():
    """tools/dlc_fixes.txt: "<dll stem, lower case> 0x<address>" per line, # comments."""
    out = {}
    if os.path.exists(FIXES):
        for line in open(FIXES, encoding='utf-8'):
            line = line.split('#', 1)[0].split()
            if len(line) == 2:
                out.setdefault(line[0].lower(), set()).add(int(line[1], 16))
    return out


def xex_base(data):
    for i in range(struct.unpack('>I', data[0x14:0x18])[0]):
        k, v = struct.unpack('>II', data[0x18 + i * 8:0x20 + i * 8])
        if k == 0x10201:
            return v
    return None


def module_block(dlls, fixes=None):
    out = [BEGIN, '# The DLC packages carry XEX DLLs (characters, enemies, shots) that the game loads with',
           '# XexLoadImage from their DLCnn: device. Generated from the installed packages.', '']
    seen = set()
    for rel in sorted(dlls, key=str.lower):
        stem = os.path.splitext(os.path.basename(rel))[0]
        key = stem.lower()
        if key in seen:
            raise SystemExit(f'two DLC DLLs named {stem}')
        seen.add(key)
        out += ['[[modules]]', 'includes = ["codegen_flags.toml"]',
                f'file_path = "game/dlc/{rel}"',
                'guest_path = "dlc:\\\\' + rel.replace('/', '\\\\') + '"',
                f'out_directory_path = "generated/dlc/{key}"', '']
        for addr in sorted((fixes or {}).get(key, ())):
            # A call target that discovery did not find (codegen Validate: "target not in any function").
            out += [f'[modules.functions.0x{addr:08X}]', f'name = "unresolved_target_{addr:08X}"', '']
    out.append(END)
    return '\n'.join(out) + '\n'


def cmd_manifest(dlc_dir, game_dir, manifest):
    if dlc_dir != '-':
        for p in glob.glob(os.path.join(dlc_dir, '*', '**', '*.dll'), recursive=True):
            rel = os.path.relpath(p, dlc_dir).replace(os.sep, '/').split('/', 1)[1]
            with open(p, 'rb') as f:
                if xex_base(f.read(0x1000)) is None:
                    raise SystemExit(f'{p}: no XEX image base')
            dst = os.path.join(game_dir, 'dlc', rel)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copyfile(p, dst)
    root = os.path.join(game_dir, 'dlc')
    dlls = [os.path.relpath(p, root).replace(os.sep, '/')
            for p in glob.glob(os.path.join(root, '**', '*.dll'), recursive=True)]
    text = open(manifest, encoding='utf-8').read()
    block = module_block(dlls, read_fixes())
    if BEGIN in text:
        text = text[:text.index(BEGIN)] + block + text[text.index(END) + len(END) + 1:]
    else:
        text = text.rstrip('\n') + '\n\n' + block
    open(manifest, 'w', encoding='utf-8', newline='\n').write(text)
    print(f'{len(dlls)} DLC modules in {manifest}')


def cmd_learn(log):
    text = open(log, encoding='utf-8', errors='replace').read()
    m = re.search(r"Analysis failed for '([^']+)'", text)
    if not m:
        raise SystemExit('no "Analysis failed" line in the log')
    stem = m.group(1).lower()
    addrs = sorted({int(a, 16) for a in re.findall(r'(0x[0-9A-Fa-f]{8}) from 0x[0-9A-Fa-f]{8}: .* - target not in any function', text)})
    have = read_fixes().get(stem, set())
    new = [a for a in addrs if a not in have]
    with open(FIXES, 'a', encoding='utf-8', newline='\n') as f:
        for a in new:
            f.write(f'{stem} 0x{a:08X}\n')
    print(f'{stem}: {len(new)} new function starts ({len(addrs)} in the log)')
    return len(new)


def slots(modules):
    """modules: [(code_base, code_size)] -> {code_base: table_base}; one slot per code base."""
    size = {}
    for base, code in modules:
        need = ((code + THUNK_RESERVE) * 2 + 0xFFFF) & ~0xFFFF
        size[base] = max(size.get(base, 0), need)
    out, at = {}, TABLE_START
    for base in sorted(size):
        out[base] = at
        at += size[base]
    if at > TABLE_END:
        raise SystemExit(f'DLC function tables need {at - TABLE_START:#x} bytes, more than the 64 MB slot area')
    return out, at


def cmd_tables(gen_dir):
    mods = []
    for pch in glob.glob(os.path.join(gen_dir, 'dlc', '*', '*_pch.h')):
        s = open(pch, encoding='utf-8').read()
        base = int(re.search(r'#define REX_CODE_BASE (0x[0-9A-Fa-f]+)', s).group(1), 16)
        size = int(re.search(r'#define REX_CODE_SIZE (0x[0-9A-Fa-f]+)', s).group(1), 16)
        mods.append((base, size))
    table, end = slots(mods)
    lines = ['// Generated by tools/dlc_modules.py tables: function-table slots of the DLC code modules.',
             '#pragma once', 'static constexpr unsigned long long rex_dll_table_slot(unsigned long long cb) {',
             '  switch (cb) {']
    lines += [f'    case 0x{b:08X}ull: return 0x{t:08X}ull;' for b, t in sorted(table.items())]
    lines += ['    default: return 0;', '  }', '}', '#define REX_DLL_TABLE_SLOT(cb) rex_dll_table_slot(cb)', '']
    open(os.path.join(gen_dir, 'rex_dll_tables.h'), 'w', newline='\n').write('\n'.join(lines))
    print(f'{len(mods)} modules, {len(table)} slots, {end - TABLE_START:#x} bytes of tables')


def selftest():
    t, end = slots([(0x84000000, 0x1000), (0x83500000, 0x20000), (0x84000000, 0x30000)])
    assert t[0x83500000] == 0x8C000000
    assert t[0x84000000] == 0x8C000000 + 0x60000          # (0x20000 + 0x10000) * 2
    assert end == t[0x84000000] + 0x80000                  # the larger of two modules at one base
    block = module_block(['data/player/dllSimon.dll'])
    assert 'guest_path = "dlc:\\\\data\\\\player\\\\dllSimon.dll"' in block, block
    assert 'out_directory_path = "generated/dlc/dllsimon"' in block
    block = module_block(['dll/enemy/DLL_FcDracula.dll'], {'dll_fcdracula': {0x8BF26638}})
    assert '[modules.functions.0x8BF26638]' in block, block


if __name__ == '__main__':
    if sys.argv[1:] == ['--selftest']:
        selftest()
        print('selftest ok')
    elif len(sys.argv) == 5 and sys.argv[1] == 'manifest':
        cmd_manifest(*sys.argv[2:])
    elif len(sys.argv) == 3 and sys.argv[1] == 'learn':
        sys.exit(0 if cmd_learn(sys.argv[2]) else 1)
    elif len(sys.argv) == 3 and sys.argv[1] == 'tables':
        cmd_tables(sys.argv[2])
    else:
        raise SystemExit(__doc__)
