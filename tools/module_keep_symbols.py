#!/usr/bin/env python3
"""Keep list for `objcopy --keep-global-symbols` when a recompiled DLL module is joined into one object (the
static modules of the Switch build, switch/sdk-edits.py "Guest DLL modules"): the module's strong definitions
(its guest functions, helper stubs, function tables) become local so the modules cannot clash with each other
or with the main module; weak definitions (inline functions and templates from the SDK headers: COMDAT groups
the final link deduplicates) must stay global, or the link fails with "defined in discarded section".

Every guest function is also a weak symbol: DEFINE_REX_FUNC(name) makes `name` a weak alias of the strong
body `__imp__name` (so a hook can replace it). Those aliases must become local too: `xstart`,
`__savegprlr_N`, `__restgprlr_N` exist in every module, and with the aliases kept global the final link
took the Alucard DLL's `xstart` for the main XEX's entry point (build 0.134: the guest's main thread ended
at once). Rule: a weak symbol whose `__imp__` twin is defined in the same object is a guest function alias
and is not kept.

    module_keep_symbols.py <nm> <joined object> <out list> <symbol to keep>...
"""
import subprocess
import sys


def keep_list(nm_lines, keep):
    syms = {}
    for line in nm_lines:
        parts = line.split()
        if len(parts) >= 3:
            syms[parts[2]] = parts[1]
    names = set(keep)
    for name, kind in syms.items():
        if kind in ('W', 'V', 'u', 'w') and ('__imp__' + name) not in syms:
            names.add(name)
    return sorted(names)


def main():
    nm, obj, out, *keep = sys.argv[1:]
    res = subprocess.run([nm, '-g', '--defined-only', obj], capture_output=True, text=True, check=True)
    with open(out, 'w', newline='\n') as f:
        f.write('\n'.join(keep_list(res.stdout.splitlines(), keep)) + '\n')


if __name__ == '__main__':
    if len(sys.argv) == 2 and sys.argv[1] == '--self-test':
        got = keep_list([
            '0000000000000000 T __imp__sub_830227C0',
            '0000000000000000 W sub_830227C0',
            '0000000000000000 T __imp__xstart',
            '0000000000000000 W xstart',
            '0000000000000000 W _ZN3rex3log3cpuEv',
            '0000000000000000 V _ZGVZN3rex3log3cpuEvE2id',
            '0000000000000000 T ReXModule_Register_x',
        ], ['ReXModule_Register_x'])
        assert got == ['ReXModule_Register_x', '_ZGVZN3rex3log3cpuEvE2id', '_ZN3rex3log3cpuEv'], got
        print('ok')
        sys.exit(0)
    main()
