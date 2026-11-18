#!/usr/bin/env python3
import struct
import sys


MAGIC = 0x41535453
VERSION = 1
HEADER_SIZE = 20
ENTRY_SIZE = 16
STB_GLOBAL = 1
STB_WEAK = 2
STV_DEFAULT = 0
SHT_SYMTAB = 2
SHT_STRTAB = 3


def fail(message):
    raise SystemExit(message)


def section_headers(data, header):
    _, _, _, _, _, _, shoff, _, _, _, _, shentsize, shnum, shstrndx = header
    if shentsize != 64 or shnum == 0 or shstrndx >= shnum:
        fail("invalid section header table")
    end = shoff + shentsize * shnum
    if end > len(data):
        fail("truncated section header table")
    return [struct.unpack_from("<IIQQQQIIQQ", data, shoff + i * shentsize)
            for i in range(shnum)], shstrndx


def section_name(data, sections, shstrndx, index):
    shstr = sections[shstrndx]
    off, size = shstr[4], shstr[5]
    if off > len(data) or size > len(data) - off:
        fail("invalid section-name string table")
    name_off = sections[index][0]
    if name_off >= size:
        fail("section name offset is out of bounds")
    end = data.find(b"\0", off + name_off, off + size)
    if end < 0:
        fail("unterminated section name")
    try:
        return bytes(data[off + name_off:end]).decode("ascii")
    except UnicodeDecodeError:
        fail("section name is not ASCII")


def select_symbol(found, symbol, binding, value):
    previous = found.get(symbol)
    if previous is None:
        found[symbol] = (binding, value)
        return
    previous_binding, previous_value = previous
    if previous_binding != binding:
        if binding == STB_GLOBAL:
            found[symbol] = (binding, value)
        return
    if previous_value != value:
        fail("conflicting addresses for symbol " + repr(symbol))


def symbols(data, sections):
    for index, section in enumerate(sections):
        if section[1] != SHT_SYMTAB:
            continue
        off, size, entsize, link = section[4], section[5], section[9], section[6]
        if entsize != 24 or size % entsize != 0 or link >= len(sections):
            fail("invalid kernel symtab")
        if off > len(data) or size > len(data) - off:
            fail("truncated kernel symtab")
        strings = sections[link]
        if strings[1] != SHT_STRTAB or strings[4] > len(data) or \
                strings[5] > len(data) - strings[4]:
            fail("invalid kernel strtab")
        raw = bytes(data[strings[4]:strings[4] + strings[5]])
        found = {}
        for cursor in range(off, off + size, entsize):
            name, info, other, shndx, value, _ = struct.unpack_from(
                "<IBBHQQ", data, cursor)
            binding = info >> 4
            visibility = other & 0x3
            if name >= len(raw):
                fail("symbol name offset is out of bounds")
            end = raw.find(b"\0", name)
            if end < 0:
                fail("unterminated symbol name")
            if (binding not in (STB_GLOBAL, STB_WEAK) or
                    visibility != STV_DEFAULT or shndx == 0):
                continue
            if end == name:
                continue
            symbol = raw[name:end]
            select_symbol(found, symbol, binding, value)
        return sorted((symbol, value) for symbol, (_, value) in found.items())
    fail("kernel symtab is missing")


def main():
    if len(sys.argv) != 2:
        fail("usage: generate_module_symbols.py kernel.elf")
    path = sys.argv[1]
    with open(path, "rb") as stream:
        data = bytearray(stream.read())
    if len(data) < 64 or data[:4] != b"\x7fELF" or data[4] != 2 or data[5] != 1:
        fail("not an ELF64 little-endian image")
    header = struct.unpack_from("<16sHHIQQQIHHHHHH", data, 0)
    sections, shstrndx = section_headers(data, header)
    table_index = next((i for i in range(len(sections))
                        if section_name(data, sections, shstrndx, i) == ".kernel_symbols"),
                       None)
    if table_index is None:
        fail(".kernel_symbols section is missing")
    table = sections[table_index]
    section_offset, section_size = table[4], table[5]
    if section_offset + section_size > len(data):
        fail("truncated .kernel_symbols section")

    entries = symbols(data, sections)
    names = bytearray()
    encoded_entries = bytearray()
    for name, address in entries:
        name_offset = len(names)
        names.extend(name)
        names.append(0)
        encoded_entries.extend(struct.pack("<QI", address, name_offset))
        encoded_entries.extend(b"\0\0\0\0")
    strings_offset = HEADER_SIZE + len(encoded_entries)
    total_size = strings_offset + len(names)
    if total_size > section_size:
        fail(".kernel_symbols section is too small")
    encoded = bytearray(section_size)
    struct.pack_into("<IHHIII", encoded, 0, MAGIC, VERSION, ENTRY_SIZE,
                     len(entries), strings_offset, total_size)
    encoded[HEADER_SIZE:HEADER_SIZE + len(encoded_entries)] = encoded_entries
    encoded[strings_offset:total_size] = names
    data[section_offset:section_offset + section_size] = encoded
    with open(path, "wb") as stream:
        stream.write(data)


if __name__ == "__main__":
    main()
