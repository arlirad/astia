#!/usr/bin/env python3
import re
import subprocess
import sys


def main() -> int:
    if len(sys.argv) != 2:
        return 2
    output = subprocess.check_output(
        ["objdump", "-d", "--demangle", sys.argv[1]], text=True
    )
    match = re.search(
        r"<astia::usb::xhci::.*xhci_driver::probe\(.*\)>:\n(.*?)(?=\n[0-9a-f]+ <)",
        output,
        re.DOTALL,
    )
    if match is None:
        raise SystemExit("xHCI probe symbol was not found")
    frame = re.search(r"\bsub\s+\$0x([0-9a-f]+),%rsp", match.group(1))
    if frame is not None and int(frame.group(1), 16) > 0x4000:
        raise SystemExit(f"xHCI probe stack frame is too large: 0x{frame.group(1)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
