#!/usr/bin/env python3
"""Retype entries in an ELF64 image's dynamic RELA table.

GNU ld refuses to emit R_X86_64_TPOFF32 into a shared object ("local-exec is
incompatible with -shared"), so the fixture that exercises the loader's
TPOFF32 branch links TPOFF64 GOT slots and this script rewrites those entries
to TPOFF32.  The fixture accessors read the slot as a sign-extended 32-bit
offset, so the retyped image is a faithful TPOFF32 consumer.
"""
import struct
import sys

ELF64_LE = b"\x7fELF\x02\x01"
PHDR_SIZE = 56
PT_LOAD = 1
PT_DYNAMIC = 2
DT_NULL = 0
DT_RELA = 7
DT_RELASZ = 8
RELA_SIZE = 24


def dynamic_tags(image: bytearray):
    phoff = struct.unpack_from("<Q", image, 0x20)[0]
    phentsize = struct.unpack_from("<H", image, 0x36)[0]
    phnum = struct.unpack_from("<H", image, 0x38)[0]
    if phentsize < PHDR_SIZE:
        raise ValueError("invalid program header size")
    for index in range(phnum):
        offset = phoff + index * phentsize
        p_type, _, p_offset, _, _, p_filesz, _, _ = struct.unpack_from(
            "<IIQQQQQQ", image, offset
        )
        if p_type != PT_DYNAMIC:
            continue
        tags = {}
        for entry in range(p_offset, p_offset + p_filesz, 16):
            d_tag, d_val = struct.unpack_from("<qQ", image, entry)
            if d_tag == DT_NULL:
                break
            tags[d_tag] = d_val
        return tags
    raise ValueError("no PT_DYNAMIC segment")


def retype(image: bytearray, source_type: int, target_type: int) -> int:
    tags = dynamic_tags(image)
    if DT_RELA not in tags or DT_RELASZ not in tags:
        raise ValueError("no dynamic RELA table")
    rela = tags[DT_RELA]
    rela_size = tags[DT_RELASZ]
    if rela_size % RELA_SIZE != 0:
        raise ValueError("misaligned dynamic RELA table")
    changed = 0
    for offset in range(rela, rela + rela_size, RELA_SIZE):
        r_info = struct.unpack_from("<Q", image, offset + 8)[0]
        if r_info & 0xFFFFFFFF != source_type:
            continue
        struct.pack_into("<Q", image, offset + 8,
                         (r_info & 0xFFFFFFFF00000000) | target_type)
        changed += 1
    return changed


def main() -> int:
    if len(sys.argv) != 5:
        print("usage: reloc_type_rewrite.py FROM_TYPE TO_TYPE INPUT OUTPUT",
              file=sys.stderr)
        return 2
    source_type, target_type = int(sys.argv[1], 0), int(sys.argv[2], 0)
    source, destination = sys.argv[3], sys.argv[4]
    with open(source, "rb") as handle:
        image = bytearray(handle.read())
    if image[:6] != ELF64_LE:
        print(f"{source}: not a little-endian ELF64 image", file=sys.stderr)
        return 1
    try:
        changed = retype(image, source_type, target_type)
    except ValueError as error:
        print(f"{source}: {error}", file=sys.stderr)
        return 1
    if changed == 0:
        print(f"{source}: no relocation of type {source_type}", file=sys.stderr)
        return 1
    with open(destination, "wb") as handle:
        handle.write(image)
    return 0


if __name__ == "__main__":
    sys.exit(main())
