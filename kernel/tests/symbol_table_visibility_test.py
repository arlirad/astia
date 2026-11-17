#!/usr/bin/env python3
import struct
import subprocess
import sys
import tempfile
from pathlib import Path


def main():
    compiler = sys.argv[1]
    generator = sys.argv[2]
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        source = root / "symbols.cpp"
        assembly = root / "table.S"
        image = root / "symbols.elf"
        source.write_text(
            "int visible_kernel_symbol() { return 1; }\n"
            "__attribute__((visibility(\"hidden\"))) int hidden_kernel_symbol() { return 2; }\n"
            "static int local_kernel_symbol() { return 3; }\n"
            "int main() { return visible_kernel_symbol() + hidden_kernel_symbol() + local_kernel_symbol(); }\n"
        )
        assembly.write_text(
            ".section .kernel_symbols,\"a\"\n"
            ".balign 8\n"
            ".zero 65536\n"
        )
        subprocess.run([compiler, "-fno-pie", "-fno-pic", "-no-pie", str(source),
                        str(assembly), "-o", str(image)], check=True)
        subprocess.run([sys.executable, generator, str(image)], check=True)
        data = image.read_bytes()
        shoff = struct.unpack_from("<Q", data, 40)[0]
        shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 58)
        sections = [struct.unpack_from("<IIQQQQIIQQ", data, shoff + i * shentsize)
                    for i in range(shnum)]
        shstr = sections[shstrndx]
        names = data[shstr[4]:shstr[4] + shstr[5]]
        table = next(section for section in sections
                     if names[section[0]:].split(b"\0", 1)[0] == b".kernel_symbols")
        blob = data[table[4]:table[4] + table[5]]
        _, _, entry_size, count, strings_offset, total_size = struct.unpack_from(
            "<IHHIII", blob)
        assert entry_size == 16
        found = set()
        for index in range(count):
            offset = 20 + index * entry_size
            name_offset = struct.unpack_from("<I", blob, offset + 8)[0]
            found.add(blob[strings_offset + name_offset:total_size].split(b"\0", 1)[0])
        assert b"_Z21visible_kernel_symbolv" in found
        assert not any(b"hidden_kernel_symbol" in name for name in found)
        assert not any(b"local_kernel_symbol" in name for name in found)


if __name__ == "__main__":
    main()
