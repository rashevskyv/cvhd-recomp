#!/usr/bin/env python3
"""Install Xbox 360 DLC packages (STFS LIVE, content type 2) for the port.

    python3 -I tools/dlc_install.py <user_root> <package> [<package> ...]

<user_root> is the port's user data folder: on the Switch sdmc:/switch/cvhd/user, on PC
the "user" folder next to cvhd.exe (or --user_data_root). For every package it writes
    <user_root>/0000000000000000/<title>/00000002/<name>/...   the package's files
    <user_root>/0000000000000000/<title>/Headers/00000002/<name>.header
The header is the SDK's XCONTENT_AGGREGATE_DATA (0x148 bytes) plus a licence mask
0xFFFFFFFF (the game asks for the package licence when it opens it). <name> is a short
ASCII name made from the package's display name (FAT-safe, at most 42 characters).
"""
import os, re, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from stfs_extract import Stfs  # noqa: E402


def display_name(path):
    with open(path, 'rb') as f:
        f.seek(0x411)
        raw = f.read(0x80)
    return raw.decode('utf-16-be', 'ignore').split('\x00')[0].strip()


def folder_name(display, used):
    base = re.sub(r'[^A-Za-z0-9]+', '_', display).strip('_')[:40] or 'dlc'
    name, i = base, 2
    while name.lower() in used:
        name, i = f'{base[:38]}_{i}', i + 1
    used.add(name.lower())
    return name


def header(name, display, title_id):
    data = bytearray(0x148)
    struct.pack_into('>II', data, 0, 1, 2)                      # device HDD, content type 2
    disp = display.encode('utf-16-be')[:254]
    data[8:8 + len(disp)] = disp                                # display name, 128 chars BE
    fn = name.encode('ascii')
    data[0x108:0x108 + len(fn)] = fn                            # file_name_raw[42]
    struct.pack_into('>QI', data, 0x138, 0, title_id)           # xuid 0, title id
    return bytes(data) + struct.pack('<I', 0xFFFFFFFF)          # licence mask (host order)


def install(root, pkg, used):
    s = Stfs(pkg)
    if s.content_type != 2:
        raise SystemExit(f'{pkg}: content type {s.content_type:X}, not a DLC (2)')
    disp = display_name(pkg) or os.path.basename(pkg)
    name = folder_name(disp, used)
    title = f'{s.title_id:08X}'
    base = os.path.join(root, '0000000000000000', title)
    dst = os.path.join(base, '00000002', name)
    entries = s.entries()
    count = total = 0
    for i, e in enumerate(entries):
        if e['dir']:
            continue
        out = os.path.join(dst, s.path_of(entries, i))
        os.makedirs(os.path.dirname(out), exist_ok=True)
        with open(out, 'wb') as w:
            w.write(s.read_file(e))
        count += 1
        total += e['size']
    hdir = os.path.join(base, 'Headers', '00000002')
    os.makedirs(hdir, exist_ok=True)
    with open(os.path.join(hdir, name + '.header'), 'wb') as w:
        w.write(header(name, disp, s.title_id))
    print(f'{name}: "{disp}", {count} files, {total} bytes')


def selftest():
    h = header('ch07', 'Pyramid', 0x58410A7A)
    assert len(h) == 0x14C
    assert struct.unpack_from('>II', h, 0) == (1, 2)
    assert h[8:8 + 14].decode('utf-16-be') == 'Pyramid'
    assert h[0x108:0x10C] == b'ch07'
    assert struct.unpack_from('>I', h, 0x140)[0] == 0x58410A7A
    assert struct.unpack_from('<I', h, 0x148)[0] == 0xFFFFFFFF
    used = set()
    assert folder_name('BGM pack1(EN)', used) == 'BGM_pack1_EN'
    assert folder_name('BGM pack1(EN)', used) == 'BGM_pack1_EN_2'


def main():
    if len(sys.argv) == 2 and sys.argv[1] == '--selftest':
        selftest()
        print('selftest ok')
        return
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    used = set()
    for pkg in sys.argv[2:]:
        install(sys.argv[1], pkg, used)


if __name__ == '__main__':
    main()
