#!/usr/bin/env python3
import pathlib
import re
import sys


def main() -> int:
    source = pathlib.Path(sys.argv[1]).read_text()
    match = re.search(
        r"if \(!wait_status\(status_halted, status_halted\)\) \{(.*?)\n\s+\}",
        source,
        re.DOTALL,
    )
    if match is None:
        raise SystemExit("xHCI halt timeout path was not found")
    body = match.group(1)
    if "quarantined_ = true" not in body or "abandon_dma()" not in body:
        raise SystemExit("xHCI timeout path does not quarantine DMA resources")
    if ".release()" in body:
        raise SystemExit("xHCI timeout path releases DMA resources")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
