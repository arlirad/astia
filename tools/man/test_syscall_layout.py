#!/usr/bin/env python3
"""Regression tests for subsystem syscall discovery and generated links."""

from __future__ import annotations

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
        assert mode == "sync", f"production wrapper is fake async: {path}"
        assert re.search(r"static\s+i64\s+invoke\s*\(", handler)
        assert "sys::syscall_context" not in handler
        assert "syscall_mode::sync" in handler
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
        "proc/syscalls/syscall_continuation.hpp",
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


def check_syscall_man_blocks(repository: Path) -> None:
    source = repository / "kernel" / "src" / "proc" / "syscalls"
    expected = {
        "alarm", "chdir", "execve", "exit", "fork", "futex", "getcwd",
        "getpgid", "getpgrp", "getpid", "getppid", "gettid", "kill", "mmap",
        "munmap", "setpgid", "setsid", "sigaction", "sigprocmask", "sigreturn",
        "sigsuspend", "spawnve", "thread_create", "thread_create2",
        "thread_detach", "thread_exit", "thread_join", "uname", "waitpid",
        "yield",
    }
    block = re.compile(r"(?m)^[ \t]*///[ \t]*(\w+)[ \t]*-[ \t]*")
    source_blocks: dict[str, Path] = {}
    for path in source.rglob("*.cpp"):
        if "syscall_handler<" not in path.read_text():
            continue
        for name in block.findall(path.read_text()):
            assert name not in source_blocks, f"duplicate man block: {name}"
            source_blocks[name] = path
    assert set(source_blocks) == expected
    assert sum(len(block.findall(path.read_text())) for path in source.rglob("*.cpp")) == 30
    assert not any(block.search(path.read_text()) for path in source.rglob("*.hpp"))


def main() -> int:
    repository = Path(__file__).resolve().parents[2]
    generator = repository / "tools/man/generate.py"
    if sys.argv[1:] == ["--abi-only"]:
        check_public_abi(repository)
        check_no_stale_syscall_82(repository)
        return 0
    check_production_layout(repository)
    check_public_abi(repository)
    check_syscall_man_blocks(repository)
    check_no_stale_syscall_82(repository)
    with tempfile.TemporaryDirectory(dir=repository) as directory:
        root = Path(directory)
        source = root / "kernel" / "src"
        docs = root / "docs"
        output = root / "man"
        (source / "proc" / "syscalls").mkdir(parents=True)
        docs.mkdir()
        (source / "proc" / "syscalls" / "wait_example.cpp").write_text(
            "/// wait_example - Wait for a child.\\n"
            "///\\n"
            "/// Synopsis:\\n"
            "///     long wait_example(void);\\n"
            "struct wait_example : sys::syscall_handler<wait_example,\\n"
            "    sys::syscall_id::waitpid, sys::syscall_mode::sync> {};\\n"
        )
        subprocess.run(
            [
                sys.executable,
                str(generator),
                str(source),
                str(output),
                "--docs",
                str(docs),
                "--libc-source",
                str(root / "libc"),
                "--abi-header",
                str(repository / "kernel/include/astia/api/syscalls.h"),
            ],
            check=True,
        )
        index = (output / "index.html").read_text()
        assert "wait_example.2.html" in index
        assert "wait_example(2)" in (output / "wait_example.2.html").read_text()
        assert "long wait_example(void);" in (output / "wait_example.2.html").read_text()
        production_output = root / "production-man"
        subprocess.run(
            [
                sys.executable,
                str(generator),
                str(repository / "kernel/src"),
                str(production_output),
                "--docs",
                str(repository / "docs"),
            ],
            check=True,
        )
        for name in {
            "alarm", "chdir", "execve", "exit", "fork", "futex", "getcwd",
            "getpgid", "getpgrp", "getpid", "getppid", "gettid", "kill", "mmap",
            "munmap", "setpgid", "setsid", "sigaction", "sigprocmask", "sigreturn",
            "sigsuspend", "spawnve", "thread_create", "thread_create2",
            "thread_detach", "thread_exit", "thread_join", "uname", "waitpid",
            "yield",
        }:
            assert (production_output / f"{name}.2.html").exists(), name
        assert "FUTEX_WAIT blocks while the word equals value." in (
            production_output / "futex.2.html"
        ).read_text()
        abi = (repository / "kernel/include/astia/api/syscalls.h").read_text()
        public_names = {
            name.lower()
            for name in re.findall(r"\bSYS_([A-Z][A-Z0-9_]*)\b", abi)
        }
        assert all(
            (production_output / f"{name}.2.html").exists()
            for name in public_names
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
