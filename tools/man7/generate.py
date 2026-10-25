#!/usr/bin/env python3
"""Generate static man7-style syscall pages from proc syscall sources."""

from __future__ import annotations

import argparse
import html
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parent
DEFAULT_CSS = ROOT / "nodivia.css"


def sentence(text: str) -> str:
    text = text.strip().rstrip(".")
    for index, char in enumerate(text):
        if char.isalpha():
            return text[:index] + char.upper() + text[index + 1:] + "."
    return text + "."

ERROR_FALLBACKS = {
    "E2BIG": "the argument or environment list exceeds the supported limit",
    "EAGAIN": "a required resource is temporarily unavailable",
    "EBADF": "the supplied file descriptor is invalid",
    "EFAULT": "a supplied userspace pointer is invalid or inaccessible",
    "ECHILD": "the caller has no matching child process",
    "EINTR": "the operation was interrupted by a signal",
    "EINVAL": "an argument or flag has an invalid value",
    "EIO": "the underlying input/output operation failed",
    "ENOMEM": "the kernel cannot allocate the requested memory",
    "ENOSYS": "the requested operation is not implemented",
    "ENOENT": "the requested path or object does not exist",
    "ENOEXEC": "the selected file is not a valid executable",
    "EPERM": "the operation is not permitted for the current process",
    "ESRCH": "the requested process, thread, or group does not exist",
}


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
    "execve": ("long execve(const char *path, char *const argv[], char *const envp[]);", "Replaces the calling process image with the executable at path. The process ID, process group, open descriptors, and working directory remain associated with the process; the new image receives the supplied argument and environment vectors.", "-E2BIG, -EFAULT, -ENOENT, -ENOEXEC, -ENOMEM."),
    "mmap": ("long mmap(void *addr, long length, uint32_t prot, uint32_t flags, int fd, long offset);", "Maps anonymous or file-backed memory into the calling process. Anonymous mappings require fd -1 and offset 0; file mappings use the supplied descriptor and a non-negative offset. The returned address is page aligned.", "-EBADF, -EINVAL, -ENOMEM, -ENOSYS."),
    "munmap": ("long munmap(void *addr, long length);", "Removes mappings that overlap the requested virtual range. The length must be positive; the address space may split a mapping when only part of a region is removed.", "-EINVAL, -ESRCH."),
    "fork": ("long fork(void);", "Creates a child process using copy-on-write address-space mappings. The parent receives the child process ID and the child resumes at the return site with zero. File descriptors and the working directory are inherited.", "-EAGAIN, -ESRCH."),
    "futex": ("long futex(uint32_t *address, uint32_t operation, uint32_t value, const timespec *timeout);", "Waits while the user word equals value or wakes waiters sharing that word. The private flag is accepted for ABI compatibility; timed waits are not implemented.", "-EAGAIN, -EFAULT, -EINVAL, -ENOSYS."),
    "thread_create": ("long thread_create(void (*entry)(void *), void *stack_top, void *argument);", "Creates a user thread in the calling process. The entry point receives argument and the new thread uses the supplied aligned user stack. The returned value is the new thread ID.", "-EAGAIN, -EFAULT, -ESRCH."),
    "thread_create2": ("long thread_create2(void (*entry)(void *, void *), void *stack_top, void *argument0, void *argument1);", "Creates a user thread and starts it with two arguments. The entry point and stack must be valid user addresses and the stack top must be aligned.", "-EAGAIN, -EFAULT, -ESRCH."),
    "thread_exit": ("long thread_exit(long result);", "Terminates the calling user thread and records result for a joining thread. A process becomes waitable when its last live thread exits.", "Does not return."),
    "thread_join": ("long thread_join(uint32_t tid, void **result);", "Waits for the selected thread to terminate and optionally copies its result pointer to result. A thread may have at most one joiner; joining releases its process-owned reference.", "-EFAULT, -EINVAL, -ESRCH."),
    "thread_detach": ("long thread_detach(uint32_t tid);", "Marks the selected thread detached. Its resources are reclaimed after termination and no later thread_join is permitted.", "-EINVAL, -ESRCH."),
    "clock_gettime": ("long clock_gettime(int clock_id, timespec *out);", "Reads a kernel clock into a userspace timespec.", "-EFAULT, -EINVAL."),
    "io_setup": ("long io_setup(uint32_t entries, uint32_t flags, ring **out);", "Creates an asynchronous I/O ring.", "-EFAULT, -EINVAL, -ENOMEM."),
    "io_destroy": ("long io_destroy(ring *ring);", "Destroys an asynchronous I/O ring.", "-EINVAL, -ESRCH."),
    "io_enter": ("long io_enter(ring *ring, uint32_t submit, uint32_t complete, uint32_t flags);", "Submits and reaps asynchronous I/O operations.", "-EFAULT, -EINVAL, -EAGAIN."),
    "io_register": ("long io_register(void);", "Registers asynchronous I/O resources.", "-EINVAL, -ENOSYS."),
}

ERROR_DETAILS = {
    "setpgid": "-EINVAL: The PID or process-group ID is zero after special-value handling.\n-EPERM: The target is not a child of the caller.\n-EPERM: The target belongs to another session.\n-EPERM: The target is a session leader.\n-ESRCH: The target process does not exist.",
    "setsid": "-EPERM: The caller is already a process-group leader.\n-ESRCH: There is no current process.",
    "sigaction": "-EFAULT: The old or new action pointer is not accessible userspace memory.\n-EINVAL: The signal number is invalid or uncatchable.\n-ESRCH: There is no current process.",
    "sigreturn": "-EFAULT: The saved context is outside userspace.\n-EFAULT: The saved instruction pointer, stack pointer, or flags are invalid.",
    "sigprocmask": "-EFAULT: The old mask pointer is not writable userspace memory.\n-EINVAL: The operation is not BLOCK, UNBLOCK, or SETMASK.\n-ESRCH: There is no current process.",
    "sigsuspend": "-EFAULT: The mask pointer is not readable userspace memory.\n-EINTR: A signal interrupted the suspended thread.\n-ESRCH: There is no current process or thread.",
    "waitpid": "-EFAULT: The status pointer is not writable userspace memory.\n-ECHILD: The caller has no matching child.\n-EINVAL: The PID or option mask is invalid.\n-ESRCH: There is no current process or thread.",
    "spawnve": "-E2BIG: The argument or environment vector exceeds its limit.\n-EFAULT: A path, vector, or string pointer is invalid.\n-EIO: The executable cannot be read completely.\n-ENOMEM: The image or process cannot be allocated.\n-ENOENT: The executable path does not exist.\n-ENOEXEC: The file is not a valid executable.\n-EAGAIN: The process registry is full.",
    "execve": "-E2BIG: The argument or environment vector exceeds its limit.\n-EFAULT: A path, vector, or string pointer is invalid.\n-ENOENT: The executable path does not exist.\n-ENOEXEC: The file is not a valid executable.\n-ENOMEM: The replacement image cannot be allocated.",
    "mmap": "-EBADF: The file descriptor is invalid for a file mapping.\n-EINVAL: The length, offset, protection, flags, or anonymous sentinel is invalid.\n-ENOMEM: The address space or physical backing cannot be allocated.\n-ENOSYS: The requested fixed or shared mapping mode is not implemented.",
    "munmap": "-EINVAL: The length is zero or the address range is invalid.\n-ESRCH: There is no current process.",
    "fork": "-EAGAIN: A process, thread, or registry slot cannot be allocated.\n-ESRCH: There is no current user process.",
    "futex": "-EAGAIN: The futex value changed or the waiter table is full.\n-EFAULT: The futex word is not accessible userspace memory.\n-EINVAL: The operation is unknown.\n-ENOSYS: A timeout was requested but is not implemented.",
    "thread_create": "-EAGAIN: The process thread limit or memory limit was reached.\n-EFAULT: The entry point or user stack is invalid.\n-ESRCH: There is no current process.",
    "thread_create2": "-EAGAIN: The process thread limit or memory limit was reached.\n-EFAULT: The entry point, stack, or argument pointer is invalid.\n-ESRCH: There is no current process.",
    "thread_join": "-EFAULT: The result pointer is not writable userspace memory.\n-EINVAL: The thread is detached, already joined, or is the caller.\n-ESRCH: The thread does not exist.",
    "thread_detach": "-EINVAL: The thread already has a joiner.\n-ESRCH: The thread does not exist.",
    "io_setup": "-EFAULT: The output ring pointer is not writable userspace memory.\n-EINVAL: The ring parameters are invalid.\n-ENOMEM: The ring cannot be allocated.",
    "io_destroy": "-EINVAL: The ring handle is invalid.\n-ESRCH: The ring does not belong to the caller.",
    "io_enter": "-EFAULT: The timeout or ring pointer is invalid.\n-EINVAL: The submission parameters are invalid.\n-EAGAIN: The operation cannot make progress immediately.",
    "io_register": "-ENOSYS: Resource registration is not implemented yet.",
    "clock_gettime": "-EFAULT: The output timespec pointer is not writable userspace memory.\n-EINVAL: The clock ID is unsupported.",
}

API_PAGES = {
    "pipe": (
        "Synopsis:\n"
        "    int pipe(int fds[2]);\n\n"
        "Description:\n"
        "    Creates a unidirectional kernel pipe and stores the read end in "
        "fds[0] and the write end in fds[1]. Data written to the write end "
        "is read in FIFO order from the read end. A read blocks while the "
        "pipe is empty and at least one writer remains; a read returns EOF "
        "after every writer is closed.\n\n"
        "Return Value:\n"
        "    Returns zero and two open descriptors on success, or -1 with "
        "errno set on failure.\n\n"
        "Errors:\n"
        "    -EFAULT: The fds array is not writable userspace memory.\n"
        "    -EMFILE: The process has no two free descriptor slots.\n"
        "    -ESRCH: There is no current process.\n\n"
        "Notes:\n"
        "    pipe is a libc API, not a direct SYS_* trap. The wrapper submits "
        "OP_PIPE through the caller's AIO ring; the operation allocates both "
        "ends atomically and inherits through fork, spawn, and exec.\n",
        "ipc",
    )
}

LEGACY_RETURNS = {
    "execve": "Does not return on success; on failure it returns a negative errno.",
    "fork": "Returns the child PID in the parent and zero in the child.",
    "mmap": "Returns the mapped base address, or a negative errno.",
    "munmap": "Returns zero on success or a negative errno.",
    "futex": "Wait returns zero after a wake; wake returns the number of woken waiters.",
    "thread_create": "Returns the new thread ID or a negative errno.",
    "thread_create2": "Returns the new thread ID or a negative errno.",
    "thread_join": "Returns zero after the thread is reaped or a negative errno.",
    "thread_detach": "Returns zero after detaching the thread or a negative errno.",
    "yield": "Returns zero after giving another runnable thread an opportunity to run.",
}

LEGACY_NOTES = {
    "execve": "The argument and environment vectors are copied before the old image is replaced. Relative paths are resolved against the caller's working directory.",
    "fork": "Writable private mappings use copy-on-write. The child inherits the parent's descriptor table and current working directory.",
    "mmap": "Protection and mapping flags are validated by the kernel; unsupported combinations return an error instead of being silently ignored.",
    "munmap": "The address space owns mapping references, so physical backing is released only after the last mapping reference disappears.",
    "futex": "A non-zero timeout is currently rejected with ENOSYS. Untimed wait and wake are the supported synchronization operations.",
    "thread_create": "The caller retains ownership of the user stack. The new thread starts with the process TLS and descriptor context.",
    "thread_create2": "The caller retains ownership of the user stack. Both arguments are passed directly to the entry point.",
    "thread_join": "Passing a null result pointer discards the thread result. Joining the calling thread would deadlock and is rejected.",
    "thread_detach": "Detached threads cannot be joined; their kernel resources are reclaimed automatically after exit.",
}


def legacy_pages(text: str, subsystem: str) -> list[tuple[str, str, str]]:
    names = re.findall(r"\bi64\s+sys_(\w+)\s*\(", text)
    pages = []
    for name in names:
        synopsis, description, errors = LEGACY.get(
            name,
            (f"long {name}(...);", f"System call handler for `{name}`.", "See the kernel error ABI."),
        )
        errors = ERROR_DETAILS.get(name, errors)
        returns = LEGACY_RETURNS.get(name, "Returns a non-negative result on success. Negative values are ABI errno codes.")
        notes = LEGACY_NOTES.get(name, "Arguments enter through the direct syscall ABI; validation is performed before entering the owning subsystem.")
        pages.append((name, f"Synopsis:\n    {synopsis}\n\nDescription:\n{description}\n\nReturn Value:\n{returns}\n\nErrors:\n{errors}\n\nNotes:\n{notes}", subsystem))
    return pages


def page_section(name: str) -> str:
    return "3" if name in API_PAGES else "2"


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
                r";\s*|\n\s*(?=-E[A-Z])|,\s*(?=-E[A-Z])", content
            ) if item.strip()]
            if heading == "ERRORS" and not any(item.startswith("-E") for item in items):
                html_body = "<p>" + html.escape(sentence(content)) + "</p>"
                sections.append(f"<section><h2>{html.escape(heading)}</h2>{html_body}</section>")
                continue
            if heading == "ERRORS":
                items = [item.rstrip(".") + "." for item in items]
            rendered_items = []
            for item in items:
                if heading == "ERRORS":
                    match = re.match(r"(-E[A-Z0-9]+)\s*:\s*(.*)$", item)
                    if match:
                        code, reason = match.groups()
                    else:
                        code_match = re.match(r"(-E[A-Z0-9]+)\s*(.*)$", item)
                        code = code_match.group(1) if code_match else ""
                        reason = code_match.group(2).strip() if code_match else item
                        reason = reason or ERROR_FALLBACKS.get(code[1:], "the operation failed")
                    rendered_items.append(
                        f"<li><code>{html.escape(code)}</code>"
                        f"<ul class=\"man-list man-list--detail\"><li>"
                        f"{html.escape(sentence(reason))}</li></ul></li>"
                    )
                else:
                    rendered_items.append(f"<li>{html.escape(item)}</li>")
            html_body = "<ul class=\"man-list\">" + "".join(rendered_items) + "</ul>"
        elif content.startswith("    ") or heading == "SYNOPSIS":
            html_body = "<pre>" + escaped + "</pre>"
        else:
            html_body = "<p>" + escaped.replace("\n", "<br>\n") + "</p>"
        sections.append(f"<section><h2>{html.escape(heading)}</h2>{html_body}</section>")
    title = f"{name}({page_section(name)})"
    kind = "libc API" if name in API_PAGES else "system call"
    return f"""<!doctype html>
<html lang=\"en\"><head><meta charset=\"utf-8\">
<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">
<title>{html.escape(title)} - Astia {kind}</title>
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

    pages: dict[str, tuple[str, str]] = {}
    for path in sorted(args.source.rglob("*.hpp")):
        if "syscalls" not in path.parts:
            continue
        syscall_index = path.parts.index("syscalls")
        subsystem = path.parts[syscall_index - 1]
        if path.parent != Path(*path.parts[:syscall_index + 1]):
            subsystem = f"{subsystem}/{path.parent.name}"
        for name, body in comments(path.read_text()):
            pages[name] = (body, subsystem)
    for path in sorted(args.source.rglob("*.cpp")):
        if path.name != "syscalls.cpp":
            continue
        subsystem = path.parent.name
        for name, body, group in legacy_pages(path.read_text(), subsystem):
            pages.setdefault(name, (body, group))
    for path in sorted(args.source.glob("sys/time.cpp")):
        for name, body, group in legacy_pages(path.read_text(), "sys"):
            pages.setdefault(name, (body, group))
    for name, (body, group) in API_PAGES.items():
        pages.setdefault(name, (body, group))

    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "nodivia.css").write_text(args.css.read_text())
    for name, (body, _) in sorted(pages.items()):
        (args.output / f"{name}.html").write_text(render(name, body))
    groups: dict[str, dict[str, list[str]]] = {}
    for name, (_, group) in pages.items():
        parts = group.split("/", 1)
        root, child = parts[0], parts[1] if len(parts) > 1 else ""
        groups.setdefault(root, {}).setdefault(child, []).append(name)

    grouped_parts = []
    for root, children in sorted(groups.items()):
        direct = children.pop("", [])
        body = ""
        if direct:
            body += "<ul>" + "".join(
                f"<li><a href='{html.escape(name)}.html'>{html.escape(name)}({page_section(name)})</a></li>"
                for name in sorted(direct)
            ) + "</ul>"
        for child, names in sorted(children.items()):
            body += (
                f"<section class='surface__level__2 surface__minor man-subgroup'>"
                f"<h3>{html.escape(child)}</h3><ul>"
                + "".join(
                    f"<li><a href='{html.escape(name)}.html'>{html.escape(name)}({page_section(name)})</a></li>"
                    for name in sorted(names)
                )
                + "</ul></section>"
            )
        grouped_parts.append(
            f"<section class='surface__level__1 surface__minor'><h2>{html.escape(root)}</h2>{body}</section>"
        )
    grouped = "".join(grouped_parts)
    (args.output / "index.html").write_text(
        "<!doctype html><html><head><meta charset='utf-8'><title>Astia syscalls</title>"
        "<link rel='stylesheet' href='nodivia.css'></head><body><main class='surface'>"
        "<h1>Astia system calls</h1>" + grouped + "</main></body></html>\n"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
