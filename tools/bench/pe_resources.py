#!/usr/bin/env python3
"""List a PE file's resources: which icons, strings, dialogs and menus XP's files hold.

  tools/bench/pe_resources.py FILE...                  every resource: type, ID, size
  tools/bench/pe_resources.py --strings [WORD] FILE... string table entries (with WORD in them)
  tools/bench/pe_resources.py --dialogs FILE...        dialog templates: title and control texts

Only the resource section is read (public PE format); no code is looked at.
"""
import argparse
import struct

TYPES = {1: 'cursor', 2: 'bitmap', 3: 'icon', 4: 'menu', 5: 'dialog', 6: 'string', 9: 'accelerators',
         10: 'rcdata', 12: 'group_cursor', 14: 'group_icon', 16: 'version', 24: 'manifest'}


def resources(data):
    """(type, name, language, bytes) of every resource."""
    pe = struct.unpack_from('<I', data, 60)[0]
    count, optional = struct.unpack_from('<H', data, pe + 6)[0], struct.unpack_from('<H', data, pe + 20)[0]
    magic = struct.unpack_from('<H', data, pe + 24)[0]
    directories = pe + 24 + (112 if magic == 0x20b else 96)
    root_rva = struct.unpack_from('<I', data, directories + 16)[0]
    if not root_rva:
        return
    sections = []
    for i in range(count):
        offset = pe + 24 + optional + 40 * i
        size, address, raw_size, raw = struct.unpack_from('<IIII', data, offset + 8)
        sections.append((address, max(size, raw_size), raw))

    def at(rva):
        for address, size, raw in sections:
            if address <= rva < address + size:
                return raw + rva - address
        raise ValueError(f'RVA {rva:#x} outside the sections')

    root = at(root_rva)

    def walk(offset, path):
        named, ids = struct.unpack_from('<HH', data, root + offset + 12)
        for i in range(named + ids):
            name, target = struct.unpack_from('<II', data, root + offset + 16 + 8 * i)
            if name & 0x80000000:
                start = root + (name & 0x7fffffff)
                length = struct.unpack_from('<H', data, start)[0]
                name = data[start + 2:start + 2 + 2 * length].decode('utf-16le')
            if target & 0x80000000:
                yield from walk(target & 0x7fffffff, path + [name])
            else:
                rva, size = struct.unpack_from('<II', data, root + target)
                yield (*path, name, data[at(rva):at(rva) + size])

    yield from walk(0, [])


def strings(blob, block):
    """(ID, text) of a string table block."""
    offset = 0
    for i in range(16):
        if offset + 2 > len(blob):
            return
        length = struct.unpack_from('<H', blob, offset)[0]
        offset += 2
        if length:
            yield (block - 1) * 16 + i, blob[offset:offset + 2 * length].decode('utf-16le', 'replace')
        offset += 2 * length


def dialog(blob):
    """(title, [(ID, class, text, x, y, cx, cy)]) of a dialog template."""
    def sz(offset):
        if struct.unpack_from('<H', blob, offset)[0] == 0xffff:
            return struct.unpack_from('<H', blob, offset + 2)[0], offset + 4
        end = offset
        while blob[end:end + 2] != b'\0\0':
            end += 2
        return blob[offset:end].decode('utf-16le', 'replace'), end + 2

    extended = blob[:4] == b'\x01\0\xff\xff'
    if extended:
        style = struct.unpack_from('<I', blob, 12)[0]
        count, offset = struct.unpack_from('<H', blob, 16)[0], 26
    else:
        style = struct.unpack_from('<I', blob, 0)[0]
        count, offset = struct.unpack_from('<H', blob, 8)[0], 18
    box = struct.unpack_from('<hhhh', blob, offset - 8)
    _, offset = sz(offset)
    _, offset = sz(offset)
    title, offset = sz(offset)
    if style & 0x40:  # DS_SETFONT
        offset += 6 if extended else 2
        _, offset = sz(offset)
    controls = []
    for _ in range(count):
        offset = (offset + 3) & ~3
        if extended:
            x, y, cx, cy = struct.unpack_from('<hhhh', blob, offset + 12)
            ident = struct.unpack_from('<I', blob, offset + 20)[0]
            offset += 24
        else:
            x, y, cx, cy = struct.unpack_from('<hhhh', blob, offset + 8)
            ident = struct.unpack_from('<H', blob, offset + 16)[0]
            offset += 18
        kind, offset = sz(offset)
        text, offset = sz(offset)
        offset += 2 + struct.unpack_from('<H', blob, offset)[0]
        controls.append((ident, kind, text, x, y, cx, cy))
    return (title, box), controls


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--strings', nargs='?', const='', metavar='WORD')
    parser.add_argument('--dialogs', action='store_true')
    parser.add_argument('files', nargs='+')
    args = parser.parse_args()
    files = args.files
    for path in files:
        with open(path, 'rb') as f:
            data = f.read()
        if len(files) > 1:
            print(f'== {path}')
        for kind, name, language, blob in resources(data):
            if args.strings is not None:
                if kind == 6:
                    for ident, text in strings(blob, name):
                        if args.strings.lower() in text.lower():
                            print(f'{ident}\t{text!r}')
            elif args.dialogs:
                if kind == 5:
                    (title, box), controls = dialog(blob)
                    print(f'dialog {name} {title!r} {box}')
                    for control in controls:
                        print('   ', *map(repr, control))
            else:
                print(f'{TYPES.get(kind, kind)}\t{name}\t{language}\t{len(blob)}')


if __name__ == '__main__':
    main()
