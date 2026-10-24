#!/usr/bin/env python3
"""Keep list for `objcopy --keep-global-symbols` when a recompiled DLL module is joined into one object (the
static modules of the Switch build, switch/sdk-edits.py "Guest DLL modules"): the module's strong definitions
(its guest functions, helper stubs, function tables) become local so the modules cannot clash with each other
or with the main module; weak definitions (inline functions and templates from the SDK headers: COMDAT groups
the final link deduplicates) must stay global, or the link fails with "defined in discarded section".

    module_keep_symbols.py <nm> <joined object> <out list> <symbol to keep>...
"""
import subprocess
import sys


def main():
    nm, obj, out, *keep = sys.argv[1:]
    res = subprocess.run([nm, '-g', '--defined-only', obj], capture_output=True, text=True, check=True)
    names = set(keep)
    for line in res.stdout.splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[1] in ('W', 'V', 'u', 'w'):
            names.add(parts[2])
    with open(out, 'w', newline='\n') as f:
        f.write('\n'.join(sorted(names)) + '\n')


if __name__ == '__main__':
    main()
