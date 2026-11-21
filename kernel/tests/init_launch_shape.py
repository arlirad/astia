#!/usr/bin/env python3
"""Guard the kernel boot path against reintroducing an init watcher task."""

import pathlib
import sys


def read(root: pathlib.Path, relative: str) -> str:
    path = root / relative
    if not path.is_file():
        raise SystemExit(f"init launch shape: missing {path}")
    return path.read_text(encoding="utf-8")


def require(haystack: str, needle: str, source: str, errors: list[str]) -> None:
    if needle not in haystack:
        errors.append(f"{source}: missing {needle!r}")


def forbid(haystack: str, needle: str, source: str, errors: list[str]) -> None:
    if needle in haystack:
        errors.append(f"{source}: unexpected {needle!r}")


def main() -> int:
    root = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(".")
    errors: list[str] = []

    kmain_source = "src/kmain.cpp"
    kmain = read(root, kmain_source)
    forbid(kmain, "create_kernel(", kmain_source, errors)
    forbid(kmain, "supervise_init", kmain_source, errors)
    forbid(kmain, "init_status", kmain_source, errors)
    if kmain.count("astia::proc::waitpid(") != 1:
        errors.append(f"{kmain_source}: expected exactly one astia::proc::waitpid call")
    require(kmain, 'astia::log::info("init launched pid=%d"', kmain_source, errors)
    require(
        kmain,
        'astia::log::error("unable to launch init path=%s"',
        kmain_source,
        errors,
    )
    require(
        kmain,
        'astia::log::error("init exited pid=%d status=%d wait=%ld"',
        kmain_source,
        errors,
    )
    require(kmain, 'kpanic("init launch failed")', kmain_source, errors)
    require(kmain, 'kpanic("init terminated")', kmain_source, errors)

    test_kmain_source = "src/test_kmain.cpp"
    test_kmain = read(root, test_kmain_source)
    forbid(test_kmain, "create_kernel(", test_kmain_source, errors)
    forbid(test_kmain, "spawn_init_worker", test_kmain_source, errors)

    if errors:
        for error in errors:
            print(f"init launch shape: {error}", file=sys.stderr)
        return 1
    print("init launch shape ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
