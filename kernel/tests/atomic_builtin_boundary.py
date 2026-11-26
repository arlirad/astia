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


# (path pattern, root, suffix); suffix None means a directory tree.
CPP_ALLOWLIST = (
    ("kernel/include/astia/atomic.hpp", "kernel", ".hpp"),
)
OUT_OF_SCOPE_ALLOWLIST = (
    ("kernel/include/astia/api/aio.h", "kernel", ".h"),
    ("kernel/tests/init_libc.c", "kernel", ".c"),
    ("libc/src/pthread.c", "libc", ".c"),
    ("libc/src/semaphore.c", "libc", ".c"),
    ("user/gnulib/**", "user", None),
)
ALLOWLIST = CPP_ALLOWLIST + OUT_OF_SCOPE_ALLOWLIST
SCAN_ROOTS = ("kernel", "modules")
CPP_SUFFIXES = (".cpp", ".hpp")
TOKEN = "__atomic_"


def allowlist_entry(relative_path):
    for entry in ALLOWLIST:
        if fnmatch.fnmatch(relative_path, entry[0]):
            return entry
    return None


def is_allowlisted(relative_path):
    return allowlist_entry(relative_path) is not None


def allowlist_category(relative_path):
    entry = allowlist_entry(relative_path)
    if entry is None:
        return None
    return "cpp" if entry in CPP_ALLOWLIST else "out-of-scope"


def validate_allowlist(root):
    errors = []
    for pattern, declared_root, declared_suffix in CPP_ALLOWLIST:
        path = Path(pattern)
        actual_root = path.parts[0] if path.parts else ""
        actual_suffix = path.suffix
        if actual_root != declared_root:
            errors.append(f"allowlist root mismatch: {pattern}")
        if actual_root not in SCAN_ROOTS:
            errors.append(f"C++ allowlist entry is outside scan roots: {pattern}")
        if actual_suffix != declared_suffix or actual_suffix not in CPP_SUFFIXES:
            errors.append(f"C++ allowlist entry does not have a scanned suffix: {pattern}")
        if not (root / pattern).is_file():
            errors.append(f"C++ allowlist entry does not exist: {pattern}")

    for pattern, declared_root, declared_suffix in OUT_OF_SCOPE_ALLOWLIST:
        path = Path(pattern)
        actual_root = path.parts[0] if path.parts else ""
        actual_suffix = path.suffix
        if actual_root != declared_root:
            errors.append(f"allowlist root mismatch: {pattern}")
        if pattern.endswith("/**"):
            if declared_suffix is not None:
                errors.append(f"tree allowlist entry declares a suffix: {pattern}")
            if not (root / path.parent).is_dir():
                errors.append(f"tree allowlist entry does not exist: {pattern}")
        else:
            if actual_suffix != declared_suffix:
                errors.append(f"allowlist suffix mismatch: {pattern}")
            if not (root / pattern).is_file():
                errors.append(f"allowlist entry does not exist: {pattern}")
        if actual_root in SCAN_ROOTS and actual_suffix in CPP_SUFFIXES:
            errors.append(f"out-of-scope allowlist entry is scannable: {pattern}")

    return errors


def logical_lines(text):
    start_line = 0
    parts = []
    for line_number, line in enumerate(text.splitlines(), 1):
        if not parts:
            start_line = line_number
        if line.endswith("\\"):
            parts.append(line[:-1])
        else:
            parts.append(line)
            yield start_line, "".join(parts)
            parts = []
    if parts:
        yield start_line, "".join(parts)


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
            text = path.read_text(encoding="utf-8")
            for line_number, logical in logical_lines(text):
                if TOKEN in logical:
                    violations.append(
                        f"{relative}:{line_number}: "
                        "raw atomic builtin outside atomic.hpp"
                    )
    return violations


def write_fixture(root, relative, text=""):
    path = root / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def self_test():
    with tempfile.TemporaryDirectory(prefix="astia-atomic-boundary-") as temporary:
        root = Path(temporary)
        write_fixture(
            root,
            "kernel/include/astia/atomic.hpp",
            "int allowed() { return __atomic_load_n(nullptr, __ATOMIC_RELAXED); }\n",
        )
        write_fixture(root, "kernel/include/astia/api/aio.h")
        write_fixture(root, "kernel/tests/init_libc.c")
        write_fixture(root, "libc/src/pthread.c")
        write_fixture(root, "libc/src/semaphore.c")
        (root / "user" / "gnulib").mkdir(parents=True)
        write_fixture(
            root,
            "kernel/plain.cpp",
            "int value() { return __atomic_load_n(nullptr, __ATOMIC_RELAXED); }\n",
        )
        write_fixture(
            root,
            "kernel/split.cpp",
            "int value() { return __ato\\\n"
            "mic_load_n(nullptr, __ATOMIC_RELAXED); }\n",
        )

        violations = scan(root)
        for violation in violations:
            print(violation)

        expected = {
            "kernel/plain.cpp:1: raw atomic builtin outside atomic.hpp",
            "kernel/split.cpp:1: raw atomic builtin outside atomic.hpp",
        }
        if set(violations) != expected:
            print("atomic boundary self-test did not reject exactly the raw fixtures",
                  file=sys.stderr)
            return False
        if allowlist_category("kernel/include/astia/atomic.hpp") != "cpp":
            print("atomic boundary self-test allowlisted C++ file has no C++ reason",
                  file=sys.stderr)
            return False
        if allowlist_category("kernel/include/astia/api/aio.h") != "out-of-scope":
            print("atomic boundary self-test C header is not out of scope",
                  file=sys.stderr)
            return False

        errors = validate_allowlist(root)
        for error in errors:
            print(error, file=sys.stderr)
        if errors:
            return False

        print("atomic boundary self-test: raw and backslash-split fixtures rejected, "
              "allowlisted path skipped")
        return True


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path,
                        default=Path(__file__).resolve().parents[2])
    parser.add_argument("--self-test", action="store_true")
    arguments = parser.parse_args()

    allowlist_errors = validate_allowlist(arguments.root)
    for error in allowlist_errors:
        print(error, file=sys.stderr)

    violations = scan(arguments.root)
    for violation in violations:
        print(violation)

    failed = bool(violations) or bool(allowlist_errors)
    if arguments.self_test and not self_test():
        failed = True

    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
