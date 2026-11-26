#!/usr/bin/env python3
"""Check architecture header include boundaries and standalone reachability.

The kernel splits architecture headers across a generic tree
(``include/astia/arch``) and per-architecture overlay trees
(``include/arch/<arch>``).  Generic code must only see the generic tree, while
an architecture owns its overlay and its private generic-tree directory.
"""

import argparse
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys
import tempfile

ARCHES = ("x86_64", "aarch64")
SOURCE_SUFFIXES = (".h", ".hpp", ".c", ".cpp", ".S", ".s")
INCLUDE_PATTERN = re.compile(r'^\s*#\s*include\s*([<"])([^>"]+)[>"]')
COMPILE_FLAGS = (
    "-std=c++20",
    "-Wall",
    "-Wextra",
    "-Werror",
    "-pedantic",
    "-fsyntax-only",
)

# (source path relative to the kernel root, written include target) -> reason.
# Only the architecture dispatcher may name both private trees.
INCLUDE_ALLOWLIST = {
    ("include/astia/arch/user_layout.hpp", "astia/arch/x86_64/user_layout.hpp"):
        "user_layout dispatches to the x86_64 policy",
    ("include/astia/arch/user_layout.hpp", "astia/arch/aarch64/user_layout.hpp"):
        "user_layout dispatches to the AArch64 policy",
}

# AArch64 userspace is intentionally not implemented yet, so these headers
# exercise their diagnostic instead of compiling successfully.
INTENTIONAL_COMPILE_FAILURES = {
    ("include/astia/arch/user_layout.hpp", "aarch64"):
        "Unsupported AArch64 user-layout policy",
    ("include/astia/arch/aarch64/user_layout.hpp", "aarch64"):
        "Unsupported AArch64 user-layout policy",
}


@dataclass(frozen=True)
class IncludeEdge:
    line: int
    delimiter: str
    target: str


def arch_component(component):
    stem = PurePosixPath(component).stem
    for arch in ARCHES:
        if (component == arch or component.startswith(arch + "-") or
                component.startswith(arch + "_") or stem.endswith("_" + arch)):
            return arch
    return None


def path_owner(relative_path):
    for part in Path(relative_path).parts:
        owner = arch_component(part)
        if owner is not None:
            return owner
    return None


def include_target_arch(target):
    normalized = PurePosixPath(target.replace("\\", "/"))
    parts = normalized.parts
    if not parts:
        return None
    if parts[0] in ARCHES:
        return parts[0]
    for index, part in enumerate(parts[:-1]):
        if part in ARCHES and index > 0 and parts[index - 1] == "arch":
            return part
    return None


def is_overlay_target(target):
    normalized = "/" + target.replace("\\", "/").lstrip("/")
    return "/include/arch/" in normalized


def iter_sources(root):
    for tree in ("include", "src"):
        tree_root = root / tree
        if not tree_root.is_dir():
            continue
        for path in sorted(tree_root.rglob("*")):
            if path.is_file() and path.suffix in SOURCE_SUFFIXES:
                yield path


def include_edges(path):
    for line_number, line in enumerate(
            path.read_text(encoding="utf-8").splitlines(), 1):
        match = INCLUDE_PATTERN.match(line)
        if match:
            yield IncludeEdge(line_number, match.group(1), match.group(2))


class ArchBoundaryScanner:
    def __init__(self, root):
        self.root = root.resolve()
        self.include_root = self.root / "include"

    def overlay_root(self, owner):
        return self.include_root / "arch" / owner

    def resolve_include(self, source, edge, owner):
        candidates = []
        if edge.delimiter == '"':
            candidates.append(source.parent / edge.target)
        if owner is not None:
            candidates.append(self.overlay_root(owner) / edge.target)
        candidates.append(self.include_root / edge.target)
        for candidate in candidates:
            try:
                resolved = candidate.resolve()
            except OSError:
                continue
            if resolved.is_file():
                return resolved
        return None

    def path_escape(self, source, edge):
        target = Path(edge.target)
        if target.is_absolute():
            return "absolute include path"
        base = source.parent if edge.delimiter == '"' else self.include_root
        resolved = (base / edge.target).resolve()
        try:
            resolved.relative_to(self.root)
        except ValueError:
            return f"path escapes include root: {resolved}"
        return None

    def scan(self):
        violations = []
        for source in iter_sources(self.root):
            relative = source.relative_to(self.root).as_posix()
            owner = path_owner(relative)
            seen = {}
            for edge in include_edges(source):
                escape = self.path_escape(source, edge)
                if escape is not None:
                    violations.append(
                        f"violation: {relative}:{edge.line} includes "
                        f"{edge.target} ({escape})")
                    continue

                resolved = self.resolve_include(source, edge, owner)
                if resolved is not None and resolved == source.resolve():
                    violations.append(
                        f"violation: {relative}:{edge.line} includes "
                        f"{edge.target} (self-include)")
                    continue
                if resolved is not None:
                    if resolved in seen:
                        violations.append(
                            f"violation: {relative}:{edge.line} includes "
                            f"{edge.target} (duplicate include first seen on "
                            f"line {seen[resolved]})")
                        continue
                    seen[resolved] = edge.line

                target_arch = include_target_arch(edge.target)
                if owner is None:
                    if target_arch is not None:
                        if (relative, edge.target) in INCLUDE_ALLOWLIST:
                            continue
                        violations.append(
                            f"violation: {relative}:{edge.line} includes "
                            f"{edge.target} (generic code includes port-private "
                            f"header for {target_arch})")
                    elif is_overlay_target(edge.target):
                        violations.append(
                            f"violation: {relative}:{edge.line} includes "
                            f"{edge.target} (generic code includes an overlay "
                            f"path)")
                elif target_arch is not None and target_arch != owner:
                    violations.append(
                        f"violation: {relative}:{edge.line} includes "
                        f"{edge.target} ({owner} code includes other "
                        f"architecture {target_arch})")
        return violations


class HeaderCompileMatrix:
    def __init__(self, root, cxx, aarch64_cxx, build_dir):
        self.root = root.resolve()
        self.include_root = self.root / "include"
        self.compilers = {"x86_64": cxx, "aarch64": aarch64_cxx}
        self.build_dir = build_dir

    def targets(self):
        targets = []
        private_root = self.include_root / "astia" / "arch"
        for path in sorted(private_root.rglob("*.hpp")):
            relative = path.relative_to(self.root).as_posix()
            owner = path_owner(relative)
            include_name = path.relative_to(self.include_root).as_posix()
            if owner is None:
                for arch in ARCHES:
                    targets.append((path, arch, include_name))
            else:
                targets.append((path, owner, include_name))
        for arch in ARCHES:
            overlay_root = self.include_root / "arch" / arch
            for path in sorted(overlay_root.rglob("*.hpp")):
                targets.append(
                    (path, arch, path.relative_to(overlay_root).as_posix()))
        return targets

    def run(self):
        violations = []
        translation_dir = self.build_dir / "translation-units"
        translation_dir.mkdir(parents=True, exist_ok=True)
        for index, (path, arch, include_name) in enumerate(self.targets()):
            compiler = self.compilers[arch]
            relative = path.relative_to(self.root).as_posix()
            expected = INTENTIONAL_COMPILE_FAILURES.get((relative, arch))
            if not compiler:
                violations.append(
                    f"violation: no {arch} C++ compiler for {relative}")
                continue
            translation_unit = translation_dir / (
                f"{index:02d}-{arch}-"
                f"{re.sub('[^A-Za-z0-9]+', '_', include_name)}.cpp")
            translation_unit.write_text(
                f"#include <{include_name}>\nint main() {{ return 0; }}\n",
                encoding="utf-8")
            command = [
                compiler,
                *COMPILE_FLAGS,
                f"-I{self.include_root / 'arch' / arch}",
                f"-I{self.include_root}",
                str(translation_unit),
            ]
            result = subprocess.run(
                command, capture_output=True, text=True, check=False)
            diagnostic = result.stdout + result.stderr
            if expected is not None:
                if result.returncode == 0:
                    violations.append(
                        f"violation: {relative} ({arch}) was expected to fail "
                        f"intentionally but compiled")
                elif expected not in diagnostic:
                    violations.append(
                        f"violation: {relative} ({arch}) failed without the "
                        f"expected diagnostic '{expected}':\n{diagnostic}")
            elif result.returncode != 0:
                violations.append(
                    f"violation: {relative} ({arch}) failed standalone "
                    f"compilation:\n{diagnostic}")
        return violations


def scan(root):
    return ArchBoundaryScanner(root).scan()


def self_test():
    errors = []

    def expect(condition, message):
        if not condition:
            errors.append(message)

    ownership_cases = (
        ("include/astia/arch/x86_64/cpu_local.hpp", "x86_64"),
        ("include/astia/arch/aarch64/user_layout.hpp", "aarch64"),
        ("include/arch/x86_64/astia/arch/interrupt_frame.hpp", "x86_64"),
        ("src/arch/aarch64/port.cpp", "aarch64"),
        ("src/boot/aarch64-virt/kmain.cpp", "aarch64"),
        ("src/test/selftest_x86_64.cpp", "x86_64"),
        ("include/astia/arch/cpu.hpp", None),
        ("src/proc/scheduler.cpp", None),
    )
    for relative, expected in ownership_cases:
        expect(path_owner(relative) == expected,
               f"ownership mismatch for {relative}")

    target_cases = (
        ("astia/arch/x86_64/cpu_local.hpp", "x86_64"),
        ("../arch/aarch64/smp.hpp", "aarch64"),
        ("include/arch/aarch64/astia/arch/exception_frame.hpp", "aarch64"),
        ("astia/arch/cpu.hpp", None),
        ("astia/common.hpp", None),
    )
    for target, expected in target_cases:
        expect(include_target_arch(target) == expected,
               f"target ownership mismatch for {target}")

    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        header = root / "include" / "astia" / "arch" / "generic.hpp"
        header.parent.mkdir(parents=True)
        header.write_text(
            "#include <astia/arch/x86_64/private.hpp>\n", encoding="utf-8")
        expect(any("port-private" in violation for violation in scan(root)),
               "self-test cross-architecture include was accepted")

    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        header = root / "include" / "astia" / "arch" / "self.hpp"
        header.parent.mkdir(parents=True)
        header.write_text("#include <astia/arch/self.hpp>\n", encoding="utf-8")
        expect(any("self-include" in violation for violation in scan(root)),
               "self-test self-include was accepted")

    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        header = root / "include" / "astia" / "arch" / "generic.hpp"
        header.parent.mkdir(parents=True)
        header.write_text("#include <astia/arch/cpu.hpp>\n", encoding="utf-8")
        expect(not scan(root), "self-test allowed include was rejected")

    for error in errors:
        print(error, file=sys.stderr)
    return not errors


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--compile-matrix", action="store_true")
    parser.add_argument("--cxx")
    parser.add_argument("--aarch64-cxx")
    parser.add_argument("--build-dir", type=Path)
    arguments = parser.parse_args()

    if not (arguments.root or arguments.self_test or
            arguments.compile_matrix):
        parser.error("one of --root, --self-test, or --compile-matrix is required")

    failed = False
    if arguments.self_test and not self_test():
        failed = True
    if arguments.root is not None:
        violations = scan(arguments.root)
        for violation in violations:
            print(violation, file=sys.stderr)
        if violations:
            failed = True
        else:
            print("architecture include boundary scan passed")
    if arguments.compile_matrix:
        if not (arguments.cxx and arguments.aarch64_cxx and
                arguments.build_dir):
            print("--compile-matrix requires --cxx, --aarch64-cxx, and "
                  "--build-dir", file=sys.stderr)
            return 2
        violations = HeaderCompileMatrix(
            arguments.root, arguments.cxx, arguments.aarch64_cxx,
            arguments.build_dir).run()
        for violation in violations:
            print(violation, file=sys.stderr)
        if violations:
            failed = True
        else:
            print("architecture header compile matrix passed")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
