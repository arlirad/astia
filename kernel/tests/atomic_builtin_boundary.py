#!/usr/bin/env python3
"""Reject raw C++ __atomic_* builtins outside the shared wrapper.

The wrapper itself, C sources, and third-party trees keep their builtins until
a C-compatible atomic API exists. Everything else under kernel/ and modules/
with a C++ extension must go through astia/atomic.hpp.
"""

import argparse
import fnmatch
from pathlib import Path
import sys
import tempfile


ALLOWLIST = (
    "kernel/include/astia/atomic.hpp",
    "kernel/include/astia/api/aio.h",
    "kernel/tests/init_libc.c",
    "libc/src/pthread.c",
    "libc/src/semaphore.c",
    "user/gnulib/**",
)
SCAN_ROOTS = ("kernel", "modules")
CPP_SUFFIXES = (".cpp", ".hpp")
TOKEN = "__atomic_"


def is_allowlisted(relative_path):
    return any(fnmatch.fnmatch(relative_path, pattern) for pattern in ALLOWLIST)


def scan(root):
    violations = []
    for scan_root in SCAN_ROOTS:
        base = root / scan_root
        if not base.is_dir():
            continue
        for path in sorted(base.rglob("*")):
            if not path.is_file() or path.suffix not in CPP_SUFFIXES:
                continue
            relative = path.relative_to(root).as_posix()
            if is_allowlisted(relative):
                continue
            for line_number, line in enumerate(
                    path.read_text(encoding="utf-8").splitlines(), 1):
                if TOKEN in line:
                    violations.append(
                        f"{relative}:{line_number}: "
                        "raw atomic builtin outside atomic.hpp"
                    )
    return violations


def self_test():
    with tempfile.TemporaryDirectory(prefix="astia-atomic-boundary-") as temporary:
        root = Path(temporary)
        fixture = root / "kernel" / "fixture.cpp"
        fixture.parent.mkdir()
        fixture.write_text(
            "int value() { return __atomic_load_n(nullptr, __ATOMIC_RELAXED); }\n",
            encoding="utf-8",
        )
        violations = scan(root)
        if not violations:
            print("atomic boundary self-test fixture was not rejected",
                  file=sys.stderr)
            return False
        for violation in violations:
            print(violation)
        return True


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path,
                        default=Path(__file__).resolve().parents[2])
    parser.add_argument("--self-test", action="store_true")
    arguments = parser.parse_args()

    violations = scan(arguments.root)
    for violation in violations:
        print(violation)

    failed = bool(violations)
    if arguments.self_test and not self_test():
        failed = True

    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
