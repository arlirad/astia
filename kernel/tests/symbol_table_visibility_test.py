#!/usr/bin/env python3
import struct
import subprocess
import sys
import tempfile
import importlib.util
from pathlib import Path


def main():
    compiler = sys.argv[1]
    generator = sys.argv[2]
    spec = importlib.util.spec_from_file_location("symbol_generator", generator)
    symbol_generator = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(symbol_generator)
    selected = {}
    symbol_generator.select_symbol(selected, b"duplicate", symbol_generator.STB_WEAK, 0x10)
    symbol_generator.select_symbol(selected, b"duplicate", symbol_generator.STB_GLOBAL, 0x20)
    assert selected[b"duplicate"] == (symbol_generator.STB_GLOBAL, 0x20)
    try:
        symbol_generator.select_symbol(selected, b"duplicate", symbol_generator.STB_GLOBAL, 0x30)
    except SystemExit:
        pass
    else:
        raise AssertionError("conflicting strong symbols were accepted")

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

        def reject(mutator):
            malformed = root / ("malformed-%d.elf" % reject.counter)
            reject.counter += 1
            corrupted = bytearray(data)
            mutator(corrupted, sections, shoff)
            malformed.write_bytes(corrupted)
            result = subprocess.run([sys.executable, generator, str(malformed)],
                                    capture_output=True, text=True)
            assert result.returncode != 0
            assert "Traceback" not in result.stderr

        reject.counter = 0
        reject(lambda corrupted, _, __: struct.pack_into("<H", corrupted, 62, 0xffff))

        def corrupt_symtab_offset(corrupted, section_headers, section_table):
            index = next(i for i, section in enumerate(section_headers)
                         if section[1] == 2)
            struct.pack_into("<Q", corrupted, section_table + index * shentsize + 24,
                             len(corrupted) + 1)

        reject(corrupt_symtab_offset)

        def corrupt_symbol_name(corrupted, section_headers, section_table):
            section = next(section for section in section_headers if section[1] == 2)
            struct.pack_into("<I", corrupted, section[4], 0xffffffff)

        reject(corrupt_symbol_name)

        def corrupt_string_terminator(corrupted, section_headers, section_table):
            section = next(section for section in section_headers if section[1] == 2)
            strings = section_headers[section[6]]
            corrupted[strings[4] + strings[5] - 1] = ord("x")

        reject(corrupt_string_terminator)


if __name__ == "__main__":
    main()
