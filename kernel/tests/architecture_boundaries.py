#!/usr/bin/env python3
"""Check the deliberately small public subsystem dependency contract.

The check is intentionally limited to direct preprocessor includes and direct
CMake target links.  It does not infer dependencies from implementation files,
because those are not public boundary contracts.  `dev/device_file.hpp` is the
one current exception: it is a VFS adapter kept under the dev namespace.
"""

import argparse
from collections import defaultdict
from pathlib import Path
import re
import sys


SUBSYSTEMS = ("core", "io", "hw", "dev", "fs", "net")
FORBIDDEN_INCLUDES = {
    "core": frozenset(("io", "hw", "dev", "fs", "net")),
    "io": frozenset(("hw", "dev", "fs", "net")),
    "hw": frozenset(("dev", "fs", "net")),
    "dev": frozenset(("net",)),
    "net": frozenset(("dev", "fs")),
}
FORBIDDEN_TARGET_LINKS = {
    "core": frozenset(("hw", "dev", "fs", "net")),
    "hw": frozenset(("dev", "fs", "net")),
    "dev": frozenset(("net",)),
    "net": frozenset(("dev", "fs")),
}
INCLUDE_PATTERN = re.compile(
    r"^\s*#\s*include\s*[<\"]([^>\"]+)[>\"]"
)
CALL_PATTERN = re.compile(r"\b(target_link_libraries)\s*\(", re.MULTILINE)


def subsystem_for_header(relative_path):
    parts = Path(relative_path).parts
    if not parts:
        return None
    if parts[0] in ("core", "hw", "dev"):
        return parts[0]
    if relative_path == "io.hpp":
        return "io"
    if relative_path == "fs.hpp":
        return "fs"
    if relative_path == "net.hpp":
        return "net"
    return None


def include_exception(source_path, target):
    if source_path == "dev/device_file.hpp" and target == "fs":
        return "device_file is the VFS adapter exception"
    return None


def scan_public_includes(root):
    include_root = root / "include" / "astia"
    graph = defaultdict(set)
    violations = []
    if not include_root.is_dir():
        return graph, [f"missing public include root: {include_root}"]

    for path in sorted(include_root.rglob("*")):
        if path.suffix not in (".h", ".hpp"):
            continue
        relative = path.relative_to(include_root).as_posix()
        source = subsystem_for_header(relative)
        if source is None:
            continue
        for line_number, line in enumerate(
                path.read_text(encoding="utf-8").splitlines(), 1):
            match = INCLUDE_PATTERN.match(line)
            if not match:
                continue
            included = match.group(1)
            if not included.startswith("astia/"):
                continue
            target_path = included[len("astia/"):]
            target = subsystem_for_header(target_path)
            if target is None:
                continue
            if source != target:
                graph[source].add(target)
            if target in FORBIDDEN_INCLUDES.get(source, ()):
                exception = include_exception(relative, target)
                if exception is None:
                    violations.append(
                        f"forbidden include: {relative}:{line_number} "
                        f"includes astia/{target_path} ({source} -> {target})"
                    )
    return graph, violations


def matching_call_bodies(text):
    for match in CALL_PATTERN.finditer(text):
        start = match.end()
        depth = 1
        quote = None
        index = start
        while index < len(text) and depth:
            character = text[index]
            if quote:
                if character == quote and text[index - 1] != "\\":
                    quote = None
            elif character in ('"', "'"):
                quote = character
            elif character == "(":
                depth += 1
            elif character == ")":
                depth -= 1
            index += 1
        if depth == 0:
            yield text[start:index - 1]


def scan_cmake_links(root):
    graph = defaultdict(set)
    violations = []
    for path in sorted(root.rglob("CMakeLists.txt")):
        text = path.read_text(encoding="utf-8")
        for body in matching_call_bodies(text):
            tokens = re.findall(r"[A-Za-z0-9_.$<>{}:-]+", body)
            if not tokens:
                continue
            source = tokens[0]
            if source not in SUBSYSTEMS:
                continue
            for target in tokens[1:]:
                if target not in SUBSYSTEMS:
                    continue
                if source != target:
                    graph[source].add(target)
                if target in FORBIDDEN_TARGET_LINKS.get(source, ()):
                    relative = path.relative_to(root).as_posix()
                    violations.append(
                        f"forbidden target dependency: {relative} "
                        f"({source} -> {target})"
                    )
    return graph, violations


def find_cycles(graph):
    cycles = []
    state = {}
    stack = []

    def visit(node):
        state[node] = 1
        stack.append(node)
        for target in sorted(graph.get(node, ())):
            if state.get(target, 0) == 0:
                visit(target)
            elif state[target] == 1:
                start = stack.index(target)
                cycles.append(stack[start:] + [target])
        stack.pop()
        state[node] = 2

    for node in sorted(set(graph) | {target for targets in graph.values()
                                      for target in targets}):
        if state.get(node, 0) == 0:
            visit(node)
    return cycles


def check(root):
    include_graph, include_violations = scan_public_includes(root)
    cmake_graph, cmake_violations = scan_cmake_links(root)
    graph = defaultdict(set)
    for source, targets in include_graph.items():
        graph[source].update(targets)
    for source, targets in cmake_graph.items():
        graph[source].update(targets)
    cycle_violations = [
        "dependency cycle: " + " -> ".join(cycle)
        for cycle in find_cycles(graph)
    ]
    return include_violations + cmake_violations + cycle_violations


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    arguments = parser.parse_args()
    violations = check(arguments.root.resolve())
    if violations:
        for violation in violations:
            print(violation, file=sys.stderr)
        return 1
    print("architecture boundary checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
