#!/usr/bin/env python3
"""Generate static Astia man pages from kernel and libc sources."""

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


# Native libc pages.  These describe the public wrappers, not the underlying
# AIO opcodes; negative AIO errno values are translated to -1 and errno.
LIBC_PAGES = {
    "open": ("#include <fcntl.h>\n#include <sys/types.h>\n\nint open(const char *path, int flags, ...);", "Opens path and returns a new descriptor. O_RDONLY, O_WRONLY, or O_RDWR select access; O_CREAT consumes a mode argument, and O_EXCL, O_TRUNC, and O_APPEND are forwarded. O_NONBLOCK is declared but is not forwarded by this implementation.", "Returns a non-negative descriptor, or -1 and sets errno.", "-EFAULT: The pathname cannot be read by the kernel.\n-EINVAL: Path resolution fails.\n-ENOENT: The path cannot be opened and creation was not requested.\n-EACCES: The requested access is not allowed by the node's owner permission bits.\n-EMFILE: The process descriptor table has no free entry.", "The optional mode argument is read only when O_CREAT is set. Relative paths are resolved against the current working directory."),
    "close": ("#include <unistd.h>\n\nint close(int fd);", "Closes fd. Other descriptors referring to the same open file description remain valid.", "Returns zero, or -1 and sets errno.", "-EBADF: fd does not refer to an open descriptor.", "The call waits for the underlying asynchronous close operation."),
    "read": ("#include <unistd.h>\n\nlong read(int fd, void *buffer, unsigned long length);", "Reads up to length bytes at the descriptor's current position into buffer. The position advances by the number of bytes read; zero is a successful end-of-file or empty-pipe result.", "Returns the byte count, possibly less than length, or -1 and sets errno.", "-EBADF: fd does not refer to an open descriptor.\n-EFAULT: buffer is not accessible for length bytes.\n-EIO: The underlying file or device reports an I/O failure.", "The synchronous libc call is implemented as one OP_READ submission on the process AIO ring. Pipe reads can wait for data."),
    "write": ("#include <unistd.h>\n\nlong write(int fd, const void *buffer, unsigned long length);", "Writes up to length bytes from buffer at the descriptor's current position. Pipe writes can be short when the 4096-byte pipe buffer has limited space.", "Returns the byte count, possibly less than length, or -1 and sets errno.", "-EBADF: fd does not refer to an open descriptor.\n-EFAULT: buffer is not accessible for length bytes.\n-EPIPE: The pipe has no readers.\n-EIO: The underlying file or device reports an I/O failure.", "The synchronous libc call is implemented as one OP_WRITE submission on the process AIO ring. O_APPEND is resolved by the kernel."),
    "dup": ("#include <unistd.h>\n\nint dup(int fd);", "Creates the lowest-numbered available descriptor referring to the same open file description as fd.", "Returns the new descriptor, or -1 and sets errno.", "-EBADF: fd does not refer to an open descriptor.\n-EMFILE: The process descriptor table has no free entry.", "The duplicate shares the underlying file position and open status with fd."),
    "dup2": ("#include <unistd.h>\n\nint dup2(int old_fd, int new_fd);", "Makes new_fd refer to the same open file description as old_fd. An existing target is replaced; equal numbers are left unchanged.", "Returns new_fd, or -1 and sets errno.", "-EBADF: old_fd is invalid or new_fd is outside the process descriptor table.", "The requested descriptor number is installed directly."),
    "pipe": ("#include <unistd.h>\n\nint pipe(int fds[2]);", "Creates an anonymous pipe and stores its read end in fds[0] and write end in fds[1]. The pipe buffer is 4096 bytes.", "Returns zero, or -1 and sets errno.", "-EFAULT: fds is not writable for two descriptor values.\n-ESRCH: There is no current process.\n-ENOMEM: The pipe object or endpoint descriptors cannot be allocated.", "An empty pipe read waits while a writer remains; closing the last writer completes a waiting read with zero."),
    "stat": ("#include <sys/stat.h>\n\nint stat(const char *path, struct stat *result);", "Obtains metadata for path and writes the Astia libc struct stat. Type, mode, size, block count, timestamps, and the VFS device/inode/link values are supplied.", "Returns zero, or -1 and sets errno.", "-EFAULT: path or result is null or inaccessible.\n-EINVAL: Path resolution fails.\n-ENOENT: The path does not name an object.", "Unsupported metadata fields remain zero rather than being inferred."),
    "fstat": ("#include <sys/stat.h>\n\nint fstat(int fd, struct stat *result);", "Obtains metadata for the open file referred to by fd and writes result.", "Returns zero, or -1 and sets errno.", "-EFAULT: result is null or not writable.\n-EBADF: fd does not refer to an open descriptor.\n-ESPIPE: The underlying object does not provide a file size.", "fstat uses the native STATX fd form."),
    "lstat": ("#include <sys/stat.h>\n\nint lstat(const char *path, struct stat *result);", "Obtains metadata for path. In the current libc this function is an alias of stat and has no distinct symlink behavior.", "Returns zero, or -1 and sets errno.", "-EFAULT: path or result is null or inaccessible.\n-EINVAL: Path resolution fails.\n-ENOENT: The path does not name an object.", "Astia currently has no separate lstat implementation; callers must not rely on no-follow semantics."),
    "mkdir": ("#include <sys/stat.h>\n\nint mkdir(const char *path, mode_t mode);", "Creates a directory at path with the supplied permission bits. Parent directories must already exist.", "Returns zero, or -1 and sets errno.", "-EFAULT: path is null or cannot be read.\n-EINVAL: Path resolution fails.\n-EEXIST: The path already exists.\n-ENOENT: The parent directory does not exist.\n-ENOSPC: The filesystem has no free entry.", "Only the low permission bits in mode are retained by tmpfs."),
    "rmdir": ("#include <unistd.h>\n\nint rmdir(const char *path);", "Removes an empty directory at path.", "Returns zero, or -1 and sets errno.", "-EFAULT: path is null or cannot be read.\n-EINVAL: Path resolution fails.\n-ENOENT: The directory does not exist.\n-ENOTDIR: path names a non-directory.\n-ENOTEMPTY: The directory contains an entry.\n-EEXIST: path is the mounted filesystem root.", "This is the native unlink operation with its remove-directory flag."),
    "unlink": ("#include <unistd.h>\n\nint unlink(const char *path);", "Removes a non-directory filesystem entry at path.", "Returns zero, or -1 and sets errno.", "-EFAULT: path is null or cannot be read.\n-EINVAL: Path resolution fails.\n-ENOENT: The path does not exist.\n-EISDIR: path names a directory.\n-EEXIST: path is the mounted filesystem root.", "The native VFS has no separate link-count behavior exposed by this API."),
    "rename": ("#include <unistd.h>\n\nint rename(const char *old_path, const char *new_path);", "Renames old_path to new_path within the same mounted filesystem. An existing target of the same type is replaced.", "Returns zero, or -1 and sets errno.", "-EFAULT: Either pathname cannot be read.\n-EINVAL: Path resolution fails or a directory is moved below itself.\n-ENOENT: A path or parent directory does not exist.\n-ENOTDIR: The source and target types are incompatible.\n-EISDIR: The source and target types are incompatible.\n-ENOTEMPTY: An existing target directory is not empty.\n-EXDEV: The paths are on different mounted filesystems.", "The libc API exposes no rename flags."),
    "chmod": ("#include <sys/stat.h>\n\nint chmod(const char *path, mode_t mode);", "Changes the permission bits of the filesystem entry at path.", "Returns zero, or -1 and sets errno.", "-EFAULT: path is null or cannot be read.\n-EINVAL: Path resolution fails.\n-ENOENT: The path does not exist.", "The current permission model stores the low 07777 bits and does not claim multi-user authorization semantics."),
    "chown": ("#include <sys/stat.h>\n\nint chown(const char *path, uid_t owner, gid_t group);", "Changes the stored owner UID and group GID of the path's target. The final path component is followed.", "Returns zero, or -1 and sets errno.", "-EFAULT: path is null or cannot be read.\n-EINVAL: Path resolution fails.\n-ENOENT: The target does not exist.", "Astia applies the change to the resolved file object. The current kernel credential model does not reject an owner or group value on authorization grounds."),
    "lchown": ("#include <sys/stat.h>\n\nint lchown(const char *path, uid_t owner, gid_t group);", "Changes the stored owner UID and group GID of the path's final object without following that final component. Intermediate components are still resolved normally.", "Returns zero, or -1 and sets errno.", "-EFAULT: path is null or cannot be read.\n-EINVAL: Path resolution fails.\n-ENOENT: The target does not exist.", "The native path layer preserves the no-follow request for the final component."),
    "fchown": ("#include <sys/stat.h>\n\nint fchown(int fd, uid_t owner, gid_t group);", "Changes the stored owner UID and group GID of the object referred to by fd.", "Returns zero, or -1 and sets errno.", "-EBADF: fd is negative or does not refer to an open descriptor.\n-EINVAL: The descriptor does not refer to a chownable object.", "The descriptor identifies the already opened object, so no pathname or final-component traversal occurs."),
    "access": ("#include <unistd.h>\n\nint access(const char *path, int mode);", "Checks whether the effective credentials can use path with the requested F_OK, R_OK, W_OK, and X_OK bits. F_OK checks existence and mode zero performs that same existence check.", "Returns zero when every requested check succeeds, or -1 and sets errno.", "-EACCES: A requested read, write, or execute permission bit is absent.\n-EFAULT: path is null or cannot be read.\n-EINVAL: mode contains a bit other than F_OK, R_OK, W_OK, or X_OK.\n-ENOENT: The path does not name an object.", "Owner bits are selected for the effective UID, group bits for the effective GID, and otherwise other bits. Supplementary groups are not consulted by this libc implementation. Effective UID zero bypasses read and write checks but still requires some execute bit for X_OK."),
    "isatty": ("#include <unistd.h>\n\nint isatty(int fd);", "Reports whether fd is one of Astia's standard console descriptors.", "Returns nonzero for descriptors 0 through 2 and zero for every other value. This implementation does not set errno for a negative result.", "No error is reported by this implementation.", "The result is a libc descriptor convention, not a probe of the descriptor's backing object. Use terminal ioctl operations when the descriptor's actual tty behavior matters."),
    "getuid": ("#include <unistd.h>\n\nuid_t getuid(void);", "Returns the calling process's real user ID.", "Returns the real UID. A syscall failure is reported as the unsigned conversion of the negative result by this wrapper.", "No userspace argument is invalid.", "The value is inherited by fork and spawn."),
    "geteuid": ("#include <unistd.h>\n\nuid_t geteuid(void);", "Returns the calling process's effective user ID.", "Returns the effective UID. A syscall failure is reported as the unsigned conversion of the negative result by this wrapper.", "No userspace argument is invalid.", "The effective UID selects owner permission bits."),
    "getgid": ("#include <unistd.h>\n\ngid_t getgid(void);", "Returns the calling process's real group ID.", "Returns the real GID. A syscall failure is reported as the unsigned conversion of the negative result by this wrapper.", "No userspace argument is invalid.", "The value is inherited by fork and spawn."),
    "getegid": ("#include <unistd.h>\n\ngid_t getegid(void);", "Returns the calling process's effective group ID.", "Returns the effective GID. A syscall failure is reported as the unsigned conversion of the negative result by this wrapper.", "No userspace argument is invalid.", "The effective GID selects group permission bits."),
    "setuid": ("#include <unistd.h>\n\nint setuid(uid_t uid);", "Sets the process real and effective user IDs to uid, subject to the kernel's current‑credential rule.", "Returns zero, or -1 and sets errno.", "-EINVAL: uid cannot be represented by the direct ABI.\n-EPERM: The requested transition is not allowed for the current effective UID.\n-ESRCH: The calling process does not exist.", "The wrapper translates a negative direct result to -1 and errno."),
    "seteuid": ("#include <unistd.h>\n\nint seteuid(uid_t uid);", "Sets the process effective user ID to uid without changing the real user ID.", "Returns zero, or -1 and sets errno.", "-EINVAL: uid cannot be represented by the direct ABI.\n-EPERM: The requested transition is not allowed for the current effective UID.\n-ESRCH: The calling process does not exist.", "The kernel permits a non‑root process to select its current real or effective UID."),
    "setgid": ("#include <unistd.h>\n\nint setgid(gid_t gid);", "Sets the process real and effective group IDs to gid.", "Returns zero, or -1 and sets errno.", "-EINVAL: gid cannot be represented by the direct ABI.\n-EPERM: The requested transition is not allowed for the current effective GID.\n-ESRCH: The calling process does not exist.", "Supplementary groups are unchanged."),
    "setegid": ("#include <unistd.h>\n\nint setegid(gid_t gid);", "Sets the process effective group ID to gid without changing the real group ID.", "Returns zero, or -1 and sets errno.", "-EINVAL: gid cannot be represented by the direct ABI.\n-EPERM: The requested transition is not allowed for the current effective GID.\n-ESRCH: The calling process does not exist.", "The kernel permits a non‑root process to select its current real or effective GID."),
    "setreuid": ("#include <unistd.h>\n\nint setreuid(uid_t real_uid, uid_t effective_uid);", "Sets the process real and effective UIDs. Astia's direct ABI uses the unsigned all‑ones value for the libc −1 sentinel.", "Returns zero, or -1 and sets errno.", "-EINVAL: An argument is outside the direct ABI range.\n-EPERM: The requested transition is not allowed.\n-ESRCH: The calling process does not exist.", "The current implementation accepts the current real or effective UID for a non‑root caller."),
    "setregid": ("#include <unistd.h>\n\nint setregid(gid_t real_gid, gid_t effective_gid);", "Sets the process real and effective GIDs. Astia's direct ABI uses the unsigned all‑ones value for the libc −1 sentinel.", "Returns zero, or -1 and sets errno.", "-EINVAL: An argument is outside the direct ABI range.\n-EPERM: The requested transition is not allowed.\n-ESRCH: The calling process does not exist.", "The current implementation accepts the current real or effective GID for a non‑root caller."),
    "getgroups": ("#include <unistd.h>\n\nint getgroups(int count, gid_t groups[]);", "Returns the process supplementary group list. With count zero, groups is not written and the required element count is returned; otherwise count must be at least that large.", "Returns the number of supplementary groups, or -1 and sets errno.", "-EFAULT: groups is not writable for the returned group list.\n-EINVAL: count is negative, greater than 16, or smaller than the stored group count.\n-ESRCH: The calling process does not exist.", "The native credential store permits at most 16 supplementary groups. The returned list excludes the effective GID unless it is also present in that list."),
    "setgroups": ("#include <unistd.h>\n\nint setgroups(int count, const gid_t groups[]);", "Replaces the process supplementary group list with count values from groups.", "Returns zero, or -1 and sets errno.", "-EFAULT: groups is not readable for count group IDs.\n-EINVAL: count is negative or greater than 16.\n-EPERM: The current effective GID is not zero.\n-ESRCH: The calling process does not exist.", "An empty list is valid and does not dereference groups. The kernel copies the list before replacing the process credential state."),
    "waitpid": ("#include <sys/wait.h>\n\nint waitpid(int pid, int *status, int options);", "Waits for a selected child and translates the direct waitpid result into the libc −1 and errno convention. Positive pid selects one child; zero selects the caller's process group; −1 selects any child; and a negative value selects a process group.", "Returns the reaped or reported child PID, zero only with WNOHANG when no event is ready, or -1 and sets errno.", "-EFAULT: status is non‑null but not writable.\n-ECHILD: No matching child exists.\n-EINVAL: pid or options is invalid.\n-ESRCH: The calling process or thread does not exist.", "The only accepted flags are WNOHANG, WUNTRACED, and WCONTINUED. WIFEXITED, WEXITSTATUS, WIFSIGNALED, WTERMSIG, WIFSTOPPED, WSTOPSIG, and WIFCONTINUED interpret the status word emitted by the kernel."),
    "utime": ("#include <sys/stat.h>\n\nint utime(const char *path, const time_t *value);", "Updates path's modification time to the current kernel realtime. The value argument is ignored by the current implementation.", "Returns zero, or -1 and sets errno.", "-EFAULT: path is null or cannot be read.\n-EINVAL: Path resolution fails.\n-ENOENT: The path does not exist.\n-EROFS: The filesystem does not support timestamp updates.", "Implemented as utimensat(AT_FDCWD, path, NULL, 0)."),
    "utimensat": ("#include <sys/stat.h>\n\nint utimensat(int dirfd, const char *path, const struct timespec times[2], int flags);", "Updates path's modification time to the current kernel realtime. Only AT_FDCWD (-100) and flags equal to zero are accepted; times is ignored.", "Returns zero, or -1 and sets errno.", "-EINVAL: dirfd is not AT_FDCWD, flags is nonzero, or path resolution fails.\n-EFAULT: path cannot be read.\n-ENOENT: The path does not exist.\n-EROFS: The filesystem does not support timestamp updates.", "This is a restricted native implementation, not a general dirfd-relative or caller-specified timestamp operation."),
    "file_permissions": ("#include <astia/fs.hpp>\n\nclass file_permissions;", "Represents the low 12 permission bits of a kernel filesystem mode. Construction masks the value to 07777; raw(), owner(), group(), and other() expose the stored value and its three permission fields. owner_allows(), group_allows(), and other_allows() test read, write, or execute bits.", "The constexpr accessors return the requested permission value or Boolean decision.", "No runtime errors are produced by this value type.", "This is a kernel C++ API, not a POSIX mode_t object. It does not carry the file type bits."),
    "file_mode": ("#include <astia/fs.hpp>\n\nclass file_mode;", "Combines a file_type with file_permissions. It identifies unknown, FIFO, character, directory, regular, and symlink nodes; exists() reports whether the type is not unknown. chmod() returns a mode with the same type and the supplied permission bits masked to 07777.", "The constexpr queries return the stored type, permissions, existence state, or permission decision.", "No runtime errors are produced by this value type.", "The type and permission components are deliberately separate. chmod() changes neither ownership nor file type."),
    "tcgetpgrp": ("#include <unistd.h>\n\npid_t tcgetpgrp(int fd);", "Returns the foreground process group associated with the terminal referred to by fd.", "Returns the foreground process-group ID, or -1 and sets errno.", "-EBADF: fd does not refer to an open descriptor.\n-EFAULT: The kernel cannot access the output argument.\n-ENOTTY: fd is not a terminal.", "The wrapper uses TIOCGPGRP. A terminal opened through /dev/tty resolves to the calling process's controlling terminal."),
    "tcsetpgrp": ("#include <unistd.h>\n\nint tcsetpgrp(int fd, int pgrp);", "Makes pgrp the foreground process group of the terminal referred to by fd.", "Returns zero, or -1 and sets errno.", "-EBADF: fd does not refer to an open descriptor.\n-EPERM: pgrp is nonpositive or is not a process group in the caller's session.\n-ENOTTY: fd is not a terminal.", "The wrapper uses TIOCGPGRP/TIOCSPGRP through ioctl. The target group must exist in the caller's session."),
    "devfs": ("#include <fcntl.h>\n\n/* mounted at /dev */", "Astia mounts the fixed devfs instance at /dev. It exposes registered character devices, including /dev/null and /dev/zero, and the dynamic /dev/tty node. /dev/tty exists for metadata lookup even when the caller has no controlling terminal, but opening it fails unless the caller has one. Device entries use character type and their registered permission mode.", "There is no callable devfs function in libc; applications use the ordinary open, read, write, and ioctl interfaces.", "-EACCES: The requested descriptor access is not allowed by the node's other permission bits.\n-ENOENT: The requested device entry is not registered or /dev/tty has no controlling terminal.", "The devfs registry holds at most 32 exact absolute paths. /dev/null discards writes and returns end of file on reads; /dev/zero fills reads with zero bytes."),
}


SYSCALL_PAGES = {
    "getuid": ("long getuid(void);", "Returns the calling process's real user ID.", "Returns the real UID, or -ESRCH when no current process exists.", "-ESRCH: There is no current process.", "The value is the process credential inherited across fork and spawn."),
    "geteuid": ("long geteuid(void);", "Returns the calling process's effective user ID.", "Returns the effective UID, or -ESRCH when no current process exists.", "-ESRCH: There is no current process.", "The effective UID selects owner permission checks."),
    "getgid": ("long getgid(void);", "Returns the calling process's real group ID.", "Returns the real GID, or -ESRCH when no current process exists.", "-ESRCH: There is no current process.", "The value is inherited across fork and spawn."),
    "getegid": ("long getegid(void);", "Returns the calling process's effective group ID.", "Returns the effective GID, or -ESRCH when no current process exists.", "-ESRCH: There is no current process.", "The effective GID selects group permission checks."),
    "setuid": ("long setuid(uint32_t uid);", "Sets the real and effective user IDs to uid. A non‑root caller may select only its current real or effective UID.", "Returns zero, or a negative errno.", "-EINVAL: uid is outside the unsigned 32‑bit range.\n-EPERM: The current effective UID is nonzero and uid is neither the real nor effective UID.\n-ESRCH: There is no current process.", "The kernel stores credentials per process."),
    "seteuid": ("long seteuid(uint32_t uid);", "Sets the effective user ID to uid. A non‑root caller may select only its current real or effective UID.", "Returns zero, or a negative errno.", "-EINVAL: uid is outside the unsigned 32‑bit range.\n-EPERM: The current effective UID is nonzero and uid is neither the real nor effective UID.\n-ESRCH: There is no current process.", "Changing the effective UID does not change the real UID."),
    "setgid": ("long setgid(uint32_t gid);", "Sets the real and effective group IDs to gid. A non‑root caller may select only its current real or effective GID.", "Returns zero, or a negative errno.", "-EINVAL: gid is outside the unsigned 32‑bit range.\n-EPERM: The current effective GID is nonzero and gid is neither the real nor effective GID.\n-ESRCH: There is no current process.", "Supplementary groups are unchanged."),
    "setegid": ("long setegid(uint32_t gid);", "Sets the effective group ID to gid. A non‑root caller may select only its current real or effective GID.", "Returns zero, or a negative errno.", "-EINVAL: gid is outside the unsigned 32‑bit range.\n-EPERM: The current effective GID is nonzero and gid is neither the real nor effective GID.\n-ESRCH: There is no current process.", "Changing the effective GID does not change the real GID."),
    "setreuid": ("long setreuid(uint32_t real_uid, uint32_t effective_uid);", "Sets the real and effective UIDs. The raw ABI represents the libc sentinel value −1 as the unsigned all‑ones value.", "Returns zero, or a negative errno.", "-EINVAL: Either argument is below the sentinel or outside the unsigned 32‑bit range.\n-EPERM: The requested UID transition is not allowed for the current effective UID.\n-ESRCH: There is no current process.", "The process implementation accepts the current real or effective UID, and root may select any 32‑bit UID."),
    "setregid": ("long setregid(uint32_t real_gid, uint32_t effective_gid);", "Sets the real and effective GIDs. The raw ABI represents the libc sentinel value −1 as the unsigned all‑ones value.", "Returns zero, or a negative errno.", "-EINVAL: Either argument is below the sentinel or outside the unsigned 32‑bit range.\n-EPERM: The requested GID transition is not allowed for the current effective GID.\n-ESRCH: There is no current process.", "The process implementation accepts the current real or effective GID, and root may select any 32‑bit GID."),
    "getgroups": ("long getgroups(int32_t count, uint32_t *groups);", "Copies up to 16 supplementary group IDs into groups. A count of zero returns the number of groups without writing groups.", "Returns the group count, or a negative errno.", "-EINVAL: count is negative or greater than 16.\n-EFAULT: groups is not writable for the returned IDs.\n-ESRCH: There is no current process.", "The count and groups pointer are interpreted independently when count is zero."),
    "setgroups": ("long setgroups(int32_t count, const uint32_t *groups);", "Replaces the supplementary group list with count IDs copied from groups.", "Returns zero, or a negative errno.", "-EINVAL: count is negative or greater than 16.\n-EFAULT: groups is not readable for count IDs.\n-EPERM: The current effective GID is nonzero.\n-ESRCH: There is no current process.", "A zero count is valid and does not dereference groups."),
    "waitpid": ("long waitpid(int pid, int *status, uint32_t options);", "Selects a child by positive PID, any child with −1, the caller's process group with zero, or a specified process group with a negative PID. It blocks until a selected child exits, stops when WUNTRACED is set, or reports a pending continuation when WCONTINUED is set. WNOHANG returns zero when no selected event is ready.", "Returns the selected child PID, zero only for WNOHANG with no ready event, or a negative errno. On exit, status uses the traditional layout: signal termination is signal & 0177, normal exit is (exit_status & 0377) << 8, stopped status is (stop_signal << 8) | 0177, and continuation is 0xffff.", "-EFAULT: status is non‑null but not writable userspace memory.\n-ECHILD: The caller has no matching child.\n-EINVAL: pid selects an invalid process or group identifier, or options contains an unknown bit.\n-ESRCH: There is no current process or thread.", "A successful exit event is reaped. Stop and continuation notifications are consumed but do not reap the child."),
}


def syscall_pages() -> list[tuple[str, str, str]]:
    pages = []
    for name, (synopsis, description, returns, errors, notes) in SYSCALL_PAGES.items():
        pages.append((name, f"Synopsis:\n    {synopsis}\n\nDescription:\n{description}\n\nReturn Value:\n{returns}\n\nErrors:\n{errors}\n\nNotes:\n{notes}", "proc/credentials" if name != "waitpid" else "proc/process"))
    return pages


def libc_pages() -> list[tuple[str, str, str, int]]:
    return [(name, f"Synopsis:\n    {synopsis}\n\nDescription:\n{description}\n\nReturn Value:\n{returns}\n\nErrors:\n{errors}\n\nNotes:\n{notes}", "libc/filesystem", 3) for name, (synopsis, description, returns, errors, notes) in LIBC_PAGES.items()]


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


def render(name: str, body: str, section: int) -> str:
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
    title = f"{name}({section})"
    return f"""<!doctype html>
<html lang=\"en\"><head><meta charset=\"utf-8\">
<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">
<title>{html.escape(title)} - Astia manual</title>
<link rel=\"stylesheet\" href=\"../nodivia.css\"></head>
<body><main class=\"surface surface__flex\">
<article class=\"surface__level__1 surface__minor\">
<h1>{html.escape(title)}</h1>
<nav><a href="index.html">Astia manual index</a></nav>
{''.join(sections)}
</article></main></body></html>\n"""


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path, help="syscall source directory")
    parser.add_argument("output", type=Path, help="generated site directory")
    parser.add_argument("--css", type=Path, default=DEFAULT_CSS)
    parser.add_argument("--libc-source", type=Path, default=None,
                        help="libc source root (defaults to the repository libc directory)")
    args = parser.parse_args()

    pages: dict[tuple[str, int], tuple[str, str]] = {}
    for path in sorted(args.source.rglob("*.hpp")):
        if "syscalls" not in path.parts:
            continue
        syscall_index = path.parts.index("syscalls")
        subsystem = path.parts[syscall_index - 1]
        if path.parent != Path(*path.parts[:syscall_index + 1]):
            subsystem = f"{subsystem}/{path.parent.name}"
        for name, body in comments(path.read_text()):
            pages[(name, 2)] = (body, subsystem)
    for path in sorted(args.source.rglob("*.cpp")):
        if path.name != "syscalls.cpp":
            continue
        subsystem = path.parent.name
        for name, body, group in legacy_pages(path.read_text(), subsystem):
            pages.setdefault((name, 2), (body, group))
    for path in sorted(args.source.glob("sys/time.cpp")):
        for name, body, group in legacy_pages(path.read_text(), "sys"):
            pages.setdefault((name, 2), (body, group))
    libc_root = args.libc_source
    if libc_root is None:
        candidate = args.source.resolve().parents[1] / "libc"
        libc_root = candidate if candidate.exists() else None
    if libc_root is not None and (libc_root / "include").exists():
        for name, body, group, section in libc_pages():
            pages.setdefault((name, section), (body, group))
    for name, body, group in syscall_pages():
        pages[(name, 2)] = (body, group)

    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "nodivia.css").write_text(args.css.read_text())
    for (name, section), (body, _) in sorted(pages.items()):
        (args.output / f"{name}.{section}.html").write_text(render(name, body, section))
    groups_by_section: dict[int, dict[str, dict[str, list[tuple[str, int]]]]] = {}
    for (name, section), (_, group) in pages.items():
        parts = group.split("/", 1)
        root, child = parts[0], parts[1] if len(parts) > 1 else ""
        groups_by_section.setdefault(section, {}).setdefault(root, {}).setdefault(child, []).append((name, section))

    grouped_parts = []
    section_labels = {2: "MAN 2 - SYSTEM CALLS", 3: "MAN 3 - LIBC APIs"}
    for section, groups in sorted(groups_by_section.items()):
        section_body = []
        for root, children in sorted(groups.items()):
            direct = children.get("", [])
            body = ""
            if direct:
                body += "<ul>" + "".join(
                        f"<li><a href='{html.escape(name)}.{page_section}.html'>{html.escape(name)}({page_section})</a></li>"
                    for name, page_section in sorted(direct)
                ) + "</ul>"
            for child, names in sorted(children.items()):
                if not child:
                    continue
                body += (
                    f"<section class='surface__level__2 surface__minor man-subgroup'>"
                    f"<h3>{html.escape(child)}</h3><ul>"
                    + "".join(
                        f"<li><a href='{html.escape(name)}.{page_section}.html'>{html.escape(name)}({page_section})</a></li>"
                        for name, page_section in sorted(names)
                    )
                    + "</ul></section>"
                )
            section_body.append(
                f"<section class='surface__level__1 surface__minor'><h3>{html.escape(root)}</h3>{body}</section>"
            )
        label = section_labels.get(section, f"MAN {section}")
        grouped_parts.append(
            f"<section class='surface__level__0 man-section'><h2>{html.escape(label)}</h2>{''.join(section_body)}</section>"
        )
    grouped = "".join(grouped_parts)
    (args.output / "index.html").write_text(
        "<!doctype html><html><head><meta charset='utf-8'><title>Astia manual</title>"
        "<link rel='stylesheet' href='nodivia.css'></head><body><main class='surface'>"
        "<h1>Astia manual</h1>" + grouped + "</main></body></html>\n"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
