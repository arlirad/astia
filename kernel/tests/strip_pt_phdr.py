#!/usr/bin/env python3
"""Copy an ELF image with its PT_PHDR entry turned into PT_NULL.

A payload without PT_PHDR is spec-legal; the interpreter has to recover the
load bias another way.  The fixture keeps the phdr table size and offsets
unchanged so the kernel still loads it, but the interpreter cannot read the
bias from the table.
"""
import struct
import sys


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: strip_pt_phdr.py INPUT OUTPUT", file=sys.stderr)
        return 2
    source, destination = sys.argv[1], sys.argv[2]
    with open(source, "rb") as handle:
        image = bytearray(handle.read())
    if image[:4] != b"\x7fELF" or image[4] != 2 or image[5] != 1:
        print(f"{source}: not a little-endian ELF64 image", file=sys.stderr)
        return 1
    phoff = struct.unpack_from("<Q", image, 0x20)[0]
    phentsize = struct.unpack_from("<H", image, 0x36)[0]
    phnum = struct.unpack_from("<H", image, 0x38)[0]
    if phentsize < 56:
        print(f"{source}: invalid program header size", file=sys.stderr)
        return 1
    for index in range(phnum):
        offset = phoff + index * phentsize
        if struct.unpack_from("<I", image, offset)[0] == 6:  # PT_PHDR
            struct.pack_into("<I", image, offset, 0)  # PT_NULL
            with open(destination, "wb") as handle:
                handle.write(image)
            return 0
    print(f"{source}: no PT_PHDR segment", file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
