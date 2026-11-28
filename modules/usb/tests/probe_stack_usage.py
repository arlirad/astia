#!/usr/bin/env python3
import re
import subprocess
import sys

FRAME_LIMIT = 0x4000
# The probe body is inlined into its operation thunk, which is the
# callback that actually runs on the stack.  Enumeration is split between the
# controller's PCI entry point and the transactional engine it delegates to.
TARGETS = ("xhci_driver::probe", "enumerate_pci_port", "enumeration_engine::run")


def frames(output: str):
    # Symbol names contain template brackets, so the name runs to the last
    # ">:" on the line rather than the first one.
    pattern = re.compile(
        r"^[0-9a-f]+ <(.+)>:\n(.*?)(?=^[0-9a-f]+ <.+>:\n|\Z)",
        re.DOTALL | re.MULTILINE,
    )
    for symbol, body in pattern.findall(output):
        sizes = [
            int(value, 16)
            for value in re.findall(r"\bsub\s+\$0x([0-9a-f]+),%rsp", body)
        ]
        yield symbol, max(sizes, default=0)


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit(f"usage: {sys.argv[0]} <usb.o>")
    output = subprocess.check_output(
        ["objdump", "-d", "--demangle", sys.argv[1]], text=True
    )
    found = dict.fromkeys(TARGETS, False)
    for symbol, size in frames(output):
        for target in TARGETS:
            if target in symbol:
                found[target] = True
        if size > FRAME_LIMIT:
            raise SystemExit(
                f"xHCI {symbol} stack frame is too large: 0x{size:x}"
            )
    for target, present in found.items():
        if not present:
            raise SystemExit(f"xHCI {target} symbol was not found")
    return 0


if __name__ == "__main__":
    sys.exit(main())
