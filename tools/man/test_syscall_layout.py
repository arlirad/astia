#!/usr/bin/env python3
"""Regression tests for subsystem syscall discovery and generated links."""

from __future__ import annotations

import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path


def check_production_layout(repository: Path) -> None:
    source = repository / "kernel" / "src"
    syscall_header = (source / "sys" / "syscall.hpp").read_text()
    enum_body = syscall_header.split("enum class syscall_id", 1)[1].split("};", 1)[0]
    ids: dict[str, int] = {}
    next_id = 0
    for line in enum_body.splitlines():
        match = re.search(r"\b([a-z_0-9]+)\s*(?:=\s*(\d+))?,", line)
        if match:
            if match.group(2):
                next_id = int(match.group(2))
            ids[match.group(1)] = next_id
            next_id += 1

    handler_files: list[Path] = []
    seen_ids: list[int] = []
    for path in source.glob("*/syscalls/**/*.cpp"):
        text = path.read_text()
        match = re.search(
            r"namespace astia::([\w:]+)::syscalls\s*\{.*?"
            r"struct ([a-z_0-9]+)\s*:\s*sys::syscall_handler<\s*\2\s*,\s*"
            r"(?:sys::syscall_id::([a-z_0-9]+)|"
            r"static_cast<sys::syscall_id>\((\d+)\))\s*,\s*"
            r"sys::syscall_mode::(sync|async)",
            text,
            re.S,
        )
        if not match:
            continue
        owner, name, id_name, numeric_id, mode = match.groups()
        assert path.stem == name, f"noncanonical syscall source name: {path}"
        assert owner.split("::")[0] == path.parts[path.parts.index("src") + 1]
        syscall_id = int(numeric_id) if numeric_id else ids[id_name]
        seen_ids.append(syscall_id)
        handler_files.append(path)
        handler = text.split("struct " + name, 1)[1].split("};", 1)[0]
        assert "/// ABI ID:" not in text
        assert "/// Sync/async mode:" not in text
        if mode == "async":
            assert name in {"execve", "spawnve"}, (
                f"unexpected async production wrapper: {path}"
            )
            assert re.search(r"static\s+core::task<i64>\s+invoke\s*\(", handler)
        else:
            assert re.search(r"static\s+i64\s+invoke\s*\(", handler)
        assert "sys::syscall_context" not in handler
        assert f"syscall_mode::{mode}" in handler
        assert "co_return sys::invoke_sync" not in handler
    assert len(handler_files) == 71
    assert sorted(seen_ids) == list(range(71))
    assert not (source / "net" / "syscalls").exists()
    assert all("syscalls" in path.parts for path in handler_files)
    assert not any("aio" in path.parts or "operation" in path.name for path in handler_files)
    assert not any("syscall_handler<" in path.read_text() for path in source.glob("*/syscalls/**/*.hpp"))
    shared_headers = {
        path.relative_to(source).as_posix()
        for path in source.glob("*/syscalls/**/*.hpp")
    }
    assert shared_headers == {
        "proc/syscalls/ipc/shm_common.hpp",
        "proc/syscalls/process/argv.hpp",
        "proc/syscalls/thread/thread_create_common.hpp",
    }
    invoke_sync_files = {
        path.relative_to(source).as_posix()
        for path in source.rglob("*.cpp")
        if "invoke_sync<" in path.read_text()
    }
    assert invoke_sync_files == set()
    assert not any(
        "invoke_sync" in path.read_text()
        for pattern in ("*.cpp", "*.hpp")
        for path in source.rglob(pattern)
    )
    direct_handler = (source / "proc/syscalls/getpid.cpp").read_text()
    assert "raw_id(process->pid())" in direct_handler
    io_setup = (source / "fs/syscalls/io_setup.cpp").read_text()
    assert re.search(r"user_ptr<astia_rings\*>\s+out", io_setup)
    futex = (source / "proc/syscalls/ipc/futex.cpp").read_text()
    assert re.search(r"user_ptr<astia_timespec>\s+timeout", futex)
    for path in source.rglob("*.cpp"):
        assert "register_syscall" not in path.read_text()
        assert "register_syscalls" not in path.read_text()
        assert "legacy_dispatch" not in path.read_text()


def check_public_abi(repository: Path) -> None:
    abi_path = repository / "kernel" / "include" / "astia" / "api" / "syscalls.h"
    abi = abi_path.read_text()
    entries = {
        name: int(number)
        for name, number in re.findall(r"\bSYS_([A-Z0-9_]+)\s*=\s*(\d+)\s*,", abi)
    }
    assert len(entries) == 71
    assert sorted(entries.values()) == list(range(71))
    assert entries["FCHDIR"] == 69
    assert entries["UNAME"] == 70
    assert not {
        "SOCKET", "SOCKETPAIR", "BIND", "CONNECT", "LISTEN", "ACCEPT", "SEND",
        "RECV", "SENDTO", "RECVFROM", "SHUTDOWN", "GETSOCKOPT", "READ", "WRITE",
    } & entries.keys()


def check_no_stale_syscall_82(repository: Path) -> None:
    stale = re.compile(
        r"(?:SYS_UNAME\s*=\s*82|syscall_id::uname\s*=\s*82|"
        r"uname\s*=\s*82\b|syscall-82-|syscallId:\s*82\b|"
        r"descriptor_ids\s*=\s*\{82\}|range\(70,\s*82\))"
    )
    allowlist = {repository / "tools" / "man" / "test_syscall_layout.py"}
    roots = [
        repository / "kernel",
        repository / "libc",
        repository / "tools",
        repository / "frontend" / "scripts",
        repository / "frontend" / "src" / "app" / "docs" / "docs.generated.ts",
    ]
    scanned = 0
    for root in roots:
        paths = [root] if root.is_file() else root.rglob("*")
        for path in paths:
            if path in allowlist or not path.is_file():
                continue
            if path.suffix not in {".c", ".cpp", ".h", ".hpp", ".py", ".mjs", ".ts", ".md"}:
                continue
            scanned += 1
            for number, line in enumerate(path.read_text(errors="ignore").splitlines(), 1):
                if stale.search(line):
                    raise AssertionError(
                        f"stale syscall 82 reference at {path.relative_to(repository)}:{number}"
                    )
    assert scanned > 0


def check_no_syscall_man_blocks(repository: Path) -> None:
    source = repository / "kernel" / "src"
    block = re.compile(r"(?m)^[ \t]*///[ \t]*(\w+)[ \t]*-[ \t]*")
    documented: list[str] = []
    for path in source.rglob("*.cpp"):
        if "syscalls" not in path.parts:
            continue
        text = path.read_text()
        if block.search(text) or re.search(r"(?m)^\s*///", text):
            documented.append(path.relative_to(repository).as_posix())
    assert documented == [], f"stale syscall documentation blocks: {documented}"
    for path in source.rglob("*.hpp"):
        if "syscalls" in path.parts:
            assert not block.search(path.read_text()), path.relative_to(repository)


def main() -> int:
    repository = Path(__file__).resolve().parents[2]
    generator = repository / "tools/man/generate.py"
    if sys.argv[1:] == ["--abi-only"]:
        check_public_abi(repository)
        check_no_stale_syscall_82(repository)
        return 0
    check_production_layout(repository)
    check_public_abi(repository)
    check_no_syscall_man_blocks(repository)
    check_no_stale_syscall_82(repository)
    with tempfile.TemporaryDirectory(dir=repository) as directory:
        root = Path(directory)
        output = root / "man"
        export = root / "section2"
        subprocess.run(
            [
                sys.executable,
                str(generator),
                str(repository / "kernel/src"),
                str(output),
                "--docs",
                str(repository / "docs"),
                "--export-section2",
                str(export),
            ],
            check=True,
            cwd=repository,
        )
        index = (output / "index.html").read_text()
        assert "MAN 2 - SYSTEM CALLS" in index
        assert "MAN 7 - OVERVIEWS" in index
        abi = (repository / "kernel/include/astia/api/syscalls.h").read_text()
        public_names = {
            name.lower()
            for name in re.findall(r"\bSYS_([A-Z][A-Z0-9_]*)\b", abi)
        }
        assert len(public_names) == 71
        for name in public_names:
            page = output / f"{name}.2.html"
            assert page.is_file(), name
            assert f"{name}(2)" in page.read_text()
        for name in ("aio", "alarm", "exec", "rcu", "sigtimedwait", "spawnve", "task"):
            assert (output / f"{name}.7.html").is_file(), name
        assert "FUTEX_WAIT blocks while the word equals value." in (
            output / "futex.2.html"
        ).read_text()
        assert not any(path.name.startswith("aarch64") for path in output.iterdir())
        manifest = json.loads((export / "manifest.json").read_text())
        assert manifest["count"] == 71
        assert [page["id"] for page in manifest["pages"]] == list(range(71))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
