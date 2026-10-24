#!/usr/bin/env python3
"""Generate static man7-style syscall pages from proc syscall sources."""

from __future__ import annotations

import argparse
import html
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parent
DEFAULT_CSS = ROOT / "nodivia.css"


def comments(text: str) -> list[tuple[str, str]]:
    pages: list[tuple[str, str]] = []
    lines = text.splitlines()
    for index, line in enumerate(lines):
        match = re.match(r"\s*///\s*(\w+)\s*-\s*(.*)$", line)
        if not match:
            continue
        block = [match.group(2)]
        cursor = index + 1
        while cursor < len(lines) and re.match(r"\s*///", lines[cursor]):
            block.append(re.sub(r"^\s*///\s?", "", lines[cursor]))
            cursor += 1
        pages.append((match.group(1), "\n".join(block).strip()))
    return pages


LEGACY = {
    "getpid": ("long getpid(void);", "Returns the calling process ID.", "Never fails."),
    "gettid": ("long gettid(void);", "Returns the calling thread ID.", "Never fails."),
    "getpgrp": ("long getpgrp(void);", "Returns the calling process group ID.", "Never fails."),
    "setpgid": ("long setpgid(int pid, int pgid);", "Moves a process into a process group.", "-EINVAL, -EPERM, -ESRCH."),
    "setsid": ("long setsid(void);", "Creates a new session for the caller.", "-EPERM, -ESRCH."),
    "sigaction": ("long sigaction(int signal, const action *, action *old);", "Gets or changes a signal disposition.", "-EFAULT, -EINVAL, -ESRCH."),
    "sigreturn": ("long sigreturn(const signal_context *context);", "Restores a saved signal execution context.", "-EFAULT."),
    "sigprocmask": ("long sigprocmask(int how, uint32_t set, uint32_t *old);", "Blocks, unblocks, or replaces the signal mask.", "-EFAULT, -EINVAL, -ESRCH."),
    "sigsuspend": ("long sigsuspend(const uint32_t *mask);", "Temporarily replaces the mask and waits for a signal.", "-EFAULT, -EINTR, -ESRCH."),
    "yield": ("long yield(void);", "Yields the current kernel scheduling turn.", "Never fails."),
    "exit": ("long exit(long status);", "Terminates the calling thread.", "Does not return."),
    "waitpid": ("long waitpid(int pid, int *status, uint32_t options);", "Waits for a child process and optionally reaps it.", "-EFAULT, -ECHILD, -EINVAL, -ESRCH."),
    "spawnve": ("long spawnve(const char *path, char *const argv[], char *const envp[]);", "Creates a process from an executable path.", "-E2BIG, -EAGAIN, -EFAULT, -EIO, -ENOMEM, -ENOENT, -ENOEXEC."),
    "execve": ("long execve(const char *path, char *const argv[], char *const envp[]);", "Replaces the current process image.", "-E2BIG, -EFAULT, -ENOENT, -ENOEXEC, -ENOMEM."),
    "mmap": ("long mmap(void *addr, long length, uint32_t prot, uint32_t flags, int fd, long offset);", "Maps anonymous or file-backed memory into the process.", "-EBADF, -EINVAL, -ENOMEM, -ENOSYS."),
    "munmap": ("long munmap(void *addr, long length);", "Removes a memory mapping range.", "-EINVAL, -ESRCH."),
    "fork": ("long fork(void);", "Creates a copy-on-write child process.", "-EAGAIN, -ESRCH."),
    "futex": ("long futex(uint32_t *address, uint32_t operation, uint32_t value);", "Waits on or wakes threads sharing a user word.", "-EAGAIN, -EFAULT, -EINVAL, -ENOSYS."),
    "thread_create": ("long thread_create(entry, stack, argument);", "Creates a user thread in the current process.", "-EAGAIN, -EFAULT, -ESRCH."),
    "thread_create2": ("long thread_create2(entry, stack, argument0, argument1);", "Creates a user thread with two arguments.", "-EAGAIN, -EFAULT, -ESRCH."),
    "thread_exit": ("long thread_exit(long result);", "Terminates the calling user thread.", "Does not return."),
    "thread_join": ("long thread_join(uint32_t tid, void *result);", "Waits for a thread and collects its result.", "-EFAULT, -EINVAL, -ESRCH."),
    "thread_detach": ("long thread_detach(uint32_t tid);", "Marks a thread as detached.", "-EINVAL, -ESRCH."),
}


def legacy_pages(text: str) -> list[tuple[str, str]]:
    names = re.findall(r"\bi64\s+sys_(\w+)\s*\(", text)
    pages = []
    for name in names:
        synopsis, description, errors = LEGACY.get(
            name,
            (f"long {name}(...);", f"System call handler for `{name}`.", "See the kernel error ABI."),
        )
        pages.append((name, f"Synopsis:\n    {synopsis}\n\nDescription:\n{description}\n\nReturn Value:\nReturns a non-negative result on success. Negative values are ABI errno codes.\n\nErrors:\n{errors}\n\nABI:\nArguments enter through the direct syscall ABI; validation is performed before entering the process subsystem."))
    return pages


def render(name: str, body: str) -> str:
    sections = []
    current = "DESCRIPTION"
    values: dict[str, list[str]] = {current: []}
    headings = {"Synopsis", "Description", "Return Value", "Errors", "Notes", "ABI"}
    for line in body.splitlines():
        heading_match = re.match(
            r"^(Synopsis|Description|Return Value|Errors|Notes|ABI):\s*(.*)$", line
        )
        heading = heading_match.group(1) if heading_match else line.rstrip(":")
        if heading in headings:
            current = heading.upper()
            values.setdefault(current, [])
            if heading_match and heading_match.group(2):
                values[current].append(heading_match.group(2))
        else:
            values[current].append(line)
    for heading, lines in values.items():
        content = "\n".join(lines).strip()
        if not content:
            continue
        escaped = html.escape(content)
        if heading in {"ERRORS", "RETURN VALUE"}:
            items = [item.strip() for item in re.split(
                r";\s*|,\s*(?=-E[A-Z])", content
            ) if item.strip()]
            html_body = "<ul class=\"man-list\">" + "".join(
                f"<li>{html.escape(item)}</li>" for item in items
            ) + "</ul>"
        elif content.startswith("    ") or heading == "SYNOPSIS":
            html_body = "<pre>" + escaped + "</pre>"
        else:
            html_body = "<p>" + escaped.replace("\n", "<br>\n") + "</p>"
        sections.append(f"<section><h2>{html.escape(heading)}</h2>{html_body}</section>")
    title = f"{name}(2)"
    return f"""<!doctype html>
<html lang=\"en\"><head><meta charset=\"utf-8\">
<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">
<title>{html.escape(title)} - Astia system call</title>
<link rel=\"stylesheet\" href=\"../nodivia.css\"></head>
<body><main class=\"surface surface__flex\">
<article class=\"surface__level__1 surface__minor\">
<h1>{html.escape(title)}</h1>
{''.join(sections)}
</article></main></body></html>\n"""


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path, help="syscall source directory")
    parser.add_argument("output", type=Path, help="generated site directory")
    parser.add_argument("--css", type=Path, default=DEFAULT_CSS)
    args = parser.parse_args()

    pages: dict[str, str] = {}
    doc_source = args.source / "syscalls" if (args.source / "syscalls").is_dir() else args.source
    for path in sorted(doc_source.rglob("*.hpp")):
        for name, body in comments(path.read_text()):
            pages[name] = body
    for path in sorted(args.source.rglob("*.cpp")):
        if path.name != "syscalls.cpp":
            continue
        for name, body in legacy_pages(path.read_text()):
            pages.setdefault(name, body)

    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "nodivia.css").write_text(args.css.read_text())
    for name, body in sorted(pages.items()):
        (args.output / f"{name}.html").write_text(render(name, body))
    (args.output / "index.html").write_text(
        "<!doctype html><html><head><meta charset='utf-8'><title>Astia syscalls</title>"
        "<link rel='stylesheet' href='nodivia.css'></head><body><main class='surface'>"
        "<h1>Astia system calls</h1><ul>"
        + "".join(f"<li><a href='{html.escape(n)}.html'>{html.escape(n)}(2)</a></li>"
                 for n in sorted(pages))
        + "</ul></main></body></html>\n"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
