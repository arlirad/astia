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
    handler_files = []
    for path in source.glob("*/syscalls/**/*.cpp"):
        if "sys::syscall_handler<" in path.read_text():
            handler_files.append(path)
    assert len(handler_files) == 82
    assert sum(path.parts[path.parts.index("src") + 1] == "net" for path in handler_files) == 12
    assert all("syscalls" in path.parts for path in handler_files)
    assert not any("aio" in path.parts or "operation" in path.name for path in handler_files)


def check_syscall_man_blocks(repository: Path) -> None:
    source = repository / "kernel" / "src" / "proc" / "syscalls"
    expected = {
        "alarm", "chdir", "execve", "exit", "fork", "futex", "getcwd",
        "getpgid", "getpgrp", "getpid", "getppid", "gettid", "kill", "mmap",
        "munmap", "setpgid", "setsid", "sigaction", "sigprocmask", "sigreturn",
        "sigsuspend", "spawnve", "thread_create", "thread_create2",
        "thread_detach", "thread_exit", "thread_join", "waitpid", "yield",
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
    assert sum(len(block.findall(path.read_text())) for path in source.rglob("*.cpp")) == 29
    assert not any(block.search(path.read_text()) for path in source.rglob("*.hpp"))


def main() -> int:
    repository = Path(__file__).resolve().parents[2]
    generator = repository / "tools/man/generate.py"
    check_production_layout(repository)
    check_syscall_man_blocks(repository)
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        source = root / "kernel" / "src"
        docs = root / "docs"
        output = root / "man"
        (source / "proc" / "syscalls").mkdir(parents=True)
        (source / "net" / "syscalls").mkdir(parents=True)
        docs.mkdir()
        (source / "proc" / "syscalls" / "wait_example.hpp").write_text(
            "/// wait_example - Wait for a child.\\n"
            "///\\n"
            "/// Synopsis:\\n"
            "///     long wait_example(void);\\n"
        )
        (source / "net" / "syscalls" / "socket_example.hpp").write_text(
            "/// socket_example - Open a socket.\\n"
            "///\\n"
            "/// Synopsis:\\n"
            "///     long socket_example(void);\\n"
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
            ],
            check=True,
        )
        index = (output / "index.html").read_text()
        assert "wait_example.2.html" in index
        assert "socket_example.2.html" in index
        assert index.index("net") < index.index("proc")
        assert "wait_example(2)" in (output / "wait_example.2.html").read_text()
        assert "socket_example(2)" in (output / "socket_example.2.html").read_text()
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
            "thread_detach", "thread_exit", "thread_join", "waitpid", "yield",
        }:
            assert (production_output / f"{name}.2.html").exists(), name
        assert "FUTEX_WAIT blocks while the word equals value." in (
            production_output / "futex.2.html"
        ).read_text()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
