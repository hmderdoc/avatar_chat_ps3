#!/usr/bin/env python3
"""Map a crash address to a function in avatar_chat_ps3.elf.

RPCS3 logs a fault as e.g.
  PPU[0x1000000] Thread (main_thread) [0x00011490]: VM: Access violation ...
The number in [brackets] is the program counter. Run:
  python3 tools/symbolize.py avatar_chat_ps3.elf 0x11490
Pure Python, reads the ELF symbol table itself (big- or little-endian ELF64).
"""
import struct
import sys


def read_symbols(path):
    data = open(path, 'rb').read()
    if data[:4] != b'\x7fELF' or data[4] != 2:
        sys.exit('not an ELF64 file')
    end = '>' if data[5] == 2 else '<'
    (e_shoff,) = struct.unpack_from(end + 'Q', data, 0x28)
    (e_shentsize, e_shnum, e_shstrndx) = struct.unpack_from(end + 'HHH', data, 0x3A)
    sections = []
    for i in range(e_shnum):
        off = e_shoff + i * e_shentsize
        (sh_name, sh_type, sh_flags, sh_addr, sh_offset, sh_size, sh_link, sh_info, sh_addralign, sh_entsize) = \
            struct.unpack_from(end + 'IIQQQQIIQQ', data, off)
        sections.append((sh_type, sh_offset, sh_size, sh_link, sh_entsize))
    syms = []
    for (sh_type, sh_offset, sh_size, sh_link, sh_entsize) in sections:
        if sh_type not in (2, 11) or sh_entsize == 0:  # SYMTAB, DYNSYM
            continue
        str_off, str_size = sections[sh_link][1], sections[sh_link][2]
        strtab = data[str_off:str_off + str_size]
        for j in range(sh_size // sh_entsize):
            (st_name, st_info, st_other, st_shndx, st_value, st_size) = \
                struct.unpack_from(end + 'IBBHQQ', data, sh_offset + j * sh_entsize)
            if st_info & 0xF != 2 or st_value == 0:  # STT_FUNC only
                continue
            name = strtab[st_name:strtab.index(b'\0', st_name)].decode('ascii', 'replace')
            syms.append((st_value, st_size, name))
    return sorted(syms)


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    syms = read_symbols(sys.argv[1])
    for a in sys.argv[2:]:
        addr = int(a, 0)
        best = None
        for value, size, name in syms:
            if value <= addr and (best is None or value >= best[0]):
                best = (value, size, name)
        if best is None:
            print('%#x: no symbol' % addr)
            continue
        value, size, name = best
        inside = size == 0 or addr < value + size
        print('%#x: %s+%#x%s' % (addr, name.lstrip('.'), addr - value, '' if inside else '  (past its end: likely the next function or padding)'))


if __name__ == '__main__':
    main()
