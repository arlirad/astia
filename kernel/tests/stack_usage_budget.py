#!/usr/bin/env python3
import re
import subprocess
import sys


def stack_frames(output: str) -> dict[str, int]:
    symbols = list(
        re.finditer(r"^(?:[0-9a-f]+) <([^>\n]+)>:$", output, re.MULTILINE)
    )
    frames = {}
    for index, symbol in enumerate(symbols):
        name = symbol.group(1)
        if "load_executable_stream" not in name and not any(
            helper in name
            for helper in (
                "async_mapping",
                "validate_load_phdr",
                "load_bias_alignment_ok",
                "vaddr_range_to_off",
                "vaddr_range_in_load",
                "vaddr_to_off",
            )
        ):
            continue
        end = symbols[index + 1].start() if index + 1 < len(symbols) else len(output)
        body = output[symbol.end() : end]
        values = [
            int(value, 16)
            for value in re.findall(
                r"\bsubq?\s+\$0x([0-9a-f]+),\s*%rsp", body
            )
        ]
        if values:
            frames[name] = max(values)
    return frames


def run_fixtures() -> int:
    output = """\
ffffffff80100000 <astia::elf::load_executable_stream(astia::elf::async_env, unsigned long)>:
  subq $0x1460, %rsp
ffffffff80101000 <astia::elf::load_executable_stream(astia::elf::async_env, unsigned long) (.resume)>:
  subq $0x1ee0, %rsp
ffffffff80102000 <astia::elf::load_executable_stream(astia::elf::async_env, unsigned long) (.destroy)>:
  subq $0x1ee0, %rsp
ffffffff80103000 <astia::elf::load_executable_stream(astia::elf::async_env, unsigned long) (.__await_suspend_wrapper__await)>:
  subq $0x30, %rsp
ffffffff80104000 <astia::elf::async_mapping::reset()>:
  subq $0x40, %rsp
ffffffff80105000 <unrelated_function()>:
  subq $0x9000, %rsp
"""
    frames = stack_frames(output)
    if max(frames.values(), default=0) != 0x1EE0:
        raise SystemExit(f"stack parser fixture mismatch: {frames}")
    gnu_output = """\
00000000001000 <astia::elf::load_executable_stream(astia::elf::async_env, unsigned long)>:
  sub    $0x1460,%rsp
00000000002000 <astia::elf::load_executable_stream(astia::elf::async_env, unsigned long) (.resume)>:
  sub    $0x1ee0, %rsp
00000000003000 <astia::elf::load_executable_stream(astia::elf::async_env, unsigned long) (.destroy)>:
  sub    $0x1ee0,%rsp
"""
    gnu_frames = stack_frames(gnu_output)
    if max(gnu_frames.values(), default=0) != 0x1EE0:
        raise SystemExit(f"GNU stack parser fixture mismatch: {gnu_frames}")
    return 0


def main() -> int:
    if len(sys.argv) == 2 and sys.argv[1] == "--fixture":
        return run_fixtures()
    if len(sys.argv) != 4:
        return 2
    _, objdump, image, budget_text = sys.argv
    budget = int(budget_text, 0)
    output = subprocess.check_output(
        [objdump, "-d", "--demangle", image], text=True
    )
    frames = stack_frames(output)
    if not frames:
        raise SystemExit("load_executable_stream stack frame was not found")
    over = {name: frame for name, frame in frames.items() if frame > budget}
    if over:
        details = ", ".join(f"{name}=0x{frame:x}" for name, frame in over.items())
        raise SystemExit(f"stack frame exceeds budget 0x{budget:x}: {details}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
