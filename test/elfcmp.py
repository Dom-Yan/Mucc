#!/usr/bin/env python3
# elfcmp.py A.o B.o
#
# Checks that two ELF object files are the same to the linker: the same
# sections with the same contents, the same relocations and the same
# symbols. test/asm.sh uses it to compare mucc's assembler with GNU as.
# Section order, symbol order and string table layout may differ, and
# debug sections are left to `objdump --dwarf=decodedline`. Prints the
# first differences and exits 1 if there are any.
import struct, sys

SKIP = ('.note.GNU-stack', '.comment', '.note.gnu.property')

def read(path):
    data = open(path, 'rb').read()
    shoff, = struct.unpack_from('<Q', data, 0x28)
    shnum, shstrndx = struct.unpack_from('<HH', data, 0x3c)
    shs = [struct.unpack_from('<IIQQQQIIQQ', data, shoff + i * 64) for i in range(shnum)]

    def cstr(off):
        return data[off:data.index(b'\0', off)].decode()

    names = [cstr(shs[shstrndx][4] + sh[0]) for sh in shs]
    secs = {}
    for i, sh in enumerate(shs):
        name, typ, flags, off, size, align = names[i], sh[1], sh[2], sh[4], sh[5], sh[8]
        contents = b'' if typ == 8 else data[off:off + size]   # SHT_NOBITS
        secs[i] = dict(name=name, type=typ, flags=flags, size=size, align=align,
                       data=contents, link=sh[6], info=sh[7], off=off)

    symtab = next(s for s in secs.values() if s['type'] == 2)
    strtab = secs[symtab['link']]
    syms = []
    for i in range(len(symtab['data']) // 24):
        st_name, info, other, shndx, value, size = struct.unpack_from('<IBBHQQ', symtab['data'], i * 24)
        nm = strtab['data'][st_name:strtab['data'].index(b'\0', st_name)].decode()
        syms.append(dict(name=nm, bind=info >> 4, type=info & 15, shndx=shndx, value=value, size=size))

    def secname(idx):
        return {0: 'UND', 0xfff1: 'ABS', 0xfff2: 'COM'}.get(idx) or secs[idx]['name']

    def symdesc(i):
        s = syms[i]
        return '[section %s]' % secname(s['shndx']) if s['type'] == 3 else s['name']

    relocs = {}
    for s in secs.values():
        if s['type'] != 4:   # SHT_RELA
            continue
        target = secs[s['info']]['name']
        rows = []
        for k in range(len(s['data']) // 24):
            off, info, addend = struct.unpack_from('<QQq', s['data'], k * 24)
            rows.append((off, info & 0xffffffff, symdesc(info >> 32), addend))
        relocs[target] = sorted(rows)

    symset = set()
    for s in syms[1:]:
        if s['type'] in (3, 4):   # section, file
            continue
        symset.add((s['name'], s['bind'], s['type'], secname(s['shndx']), s['value'], s['size']))

    content = {}
    for s in secs.values():
        if s['type'] in (1, 8) and s['name'] not in SKIP and not s['name'].startswith('.debug'):
            content[s['name']] = s
    return content, relocs, symset

def main():
    a, b = read(sys.argv[1]), read(sys.argv[2])
    errors = []
    for name in sorted(set(a[0]) | set(b[0])):
        sa, sb = a[0].get(name), b[0].get(name)
        if not sa or not sb:
            # An empty section only one side has doesn't matter.
            if (sa or sb)['size']:
                errors.append('section %s only in %s' % (name, sys.argv[1] if sa else sys.argv[2]))
            continue
        for key in ('type', 'flags', 'size', 'align'):
            if sa[key] != sb[key]:
                errors.append('%s: %s %s vs %s' % (name, key, sa[key], sb[key]))
        if sa['data'] != sb['data']:
            at = next(i for i in range(min(len(sa['data']), len(sb['data'])) + 1)
                      if i >= len(sa['data']) or i >= len(sb['data']) or sa['data'][i] != sb['data'][i])
            errors.append('%s: contents differ at offset 0x%x' % (name, at))
    for name in sorted(set(a[1]) | set(b[1])):
        if name.startswith('.debug'):
            continue
        ra, rb = a[1].get(name, []), b[1].get(name, [])
        if ra != rb:
            diff = [r for r in ra if r not in rb][:3] + ['|'] + [r for r in rb if r not in ra][:3]
            errors.append('relocations in %s differ: %s' % (name, diff))
    if a[2] != b[2]:
        errors.append('symbols differ: only in A %s, only in B %s' %
                      (sorted(a[2] - b[2])[:4], sorted(b[2] - a[2])[:4]))
    for e in errors[:6]:
        print('  ' + e)
    sys.exit(1 if errors else 0)

main()
