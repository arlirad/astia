#!/usr/bin/env python3
"""Generate static Astia man pages from kernel and libc sources."""

from __future__ import annotations

import argparse
import ctypes
from contextlib import ExitStack
import errno
import hashlib
import html
import json
import os
import posixpath
import re
import stat
import sys
import warnings
from pathlib import Path

from docs_manifest import load_authored_docs


ROOT = Path(__file__).resolve().parent
DEFAULT_CSS = ROOT / "nodivia.css"
PROC_FD_ROOT = Path("/proc/self/fd")


def sentence(text: str) -> str:
    text = text.strip().rstrip(".")
    for index, char in enumerate(text):
        if char.isalpha():
            return text[:index] + char.upper() + text[index + 1:] + "."
    return text + "."

LIBC_PAGES = {
    "poll": ("#include <poll.h>\n\nint poll(struct pollfd *fds, unsigned long count, int timeout_ms);", "Waits for native descriptor readiness. Pipe, terminal, and Unix socket readiness is reported by parked AIO operations and device wakeups; timeout_ms is -1 for an unbounded wait and zero for an immediate check.", "Returns the number of descriptors with nonzero revents, or -1 and sets errno.", "-EINVAL: count exceeds the native ring capacity or timeout_ms is less than -1.\n-EFAULT: A descriptor array is not accessible to userspace.", "The wrapper never spins and does not use host descriptors. POLLNVAL is reported for an invalid descriptor."),
    "select": ("#include <sys/select.h>\n\nint select(int count, fd_set *read_set, fd_set *write_set, fd_set *except_set, struct timeval *timeout);", "Selects readable, writable, and exceptional descriptors through the native parked readiness operation. The three sets are replaced with the descriptors that became ready.", "Returns the number of set members reported ready, or -1 and sets errno.", "-EINVAL: count is negative, exceeds FD_SETSIZE, or timeout has an invalid field.\n-ENOMEM: The temporary readiness array cannot be allocated.", "This is a small POSIX wrapper over poll(3); it preserves Astia's blocking and wakeup semantics without busy polling."),
    "open": ("#include <fcntl.h>\n#include <sys/types.h>\n\nint open(const char *path, int flags, ...);", "Opens path and returns a new descriptor. O_RDONLY, O_WRONLY, or O_RDWR select access; O_CREAT consumes a mode argument, and O_EXCL, O_TRUNC, O_APPEND, O_NONBLOCK, and O_CLOEXEC are forwarded.", "Returns a non-negative descriptor, or -1 and sets errno.", "-EFAULT: The pathname cannot be read by the kernel.\n-EINVAL: Path resolution fails.\n-ENOENT: The path cannot be opened and creation was not requested.\n-EACCES: The requested access is not allowed by the node's owner permission bits.\n-EMFILE: The process descriptor table has no free entry.", "The optional mode argument is read only when O_CREAT is set. Relative paths are resolved against the current working directory."),
    "close": ("#include <unistd.h>\n\nint close(int fd);", "Closes fd. Other descriptors referring to the same open file description remain valid.", "Returns zero, or -1 and sets errno.", "-EBADF: fd does not refer to an open descriptor.", "The call waits for the underlying asynchronous close operation."),
    "umount": ("#include <unistd.h>\n\nint umount(const char *target);", "Removes the native filesystem mount at target. Only root may request teardown; the root mount, mounts with open descriptors, and mounts containing child mounts cannot be removed.", "Returns zero, or -1 and sets errno.", "-EFAULT: target is not readable.\n-EPERM: The effective UID is not zero.\n-ENOENT: target is not a mount point.\n-EBUSY: target is the root mount, has open handles, or contains another mount.", "Unmounting only changes the in-memory native VFS namespace. It does not flush or persist data and never accesses a host filesystem. The filesystem type remains registered and may be mounted again by kernel bootstrap code."),
    "mount": ("#include <unistd.h>\n\nint mount(const char *type, const char *target, const char *source);", "Installs a registered native filesystem type at an existing directory. Only root may mount; target is normalized and source is an opaque factory argument.", "Returns zero, or -1 and sets errno.", "-EFAULT: a string is not readable.\n-EINVAL: an argument or target path is invalid.\n-EEXIST: target is already mounted.\n-ENOENT: target does not exist.\n-EBUSY: the fixed mount table is full.\n-EPERM: the effective UID is not zero.\n-ENODEV: type is not registered.", "This changes only the in-memory native VFS namespace. It never accesses or persists a host filesystem."),
    "read": ("#include <unistd.h>\n\nlong read(int fd, void *buffer, unsigned long length);", "Reads up to length bytes at the descriptor's current position into buffer. The position advances by the number of bytes read; zero is a successful end-of-file or empty-pipe result.", "Returns the byte count, possibly less than length, or -1 and sets errno.", "-EBADF: fd does not refer to an open descriptor.\n-EFAULT: buffer is not accessible for length bytes.\n-EIO: The underlying file or device reports an I/O failure.", "The synchronous libc call is implemented as one OP_READ submission on the process AIO ring. Pipe reads can wait for data."),
    "write": ("#include <unistd.h>\n\nlong write(int fd, const void *buffer, unsigned long length);", "Writes up to length bytes from buffer at the descriptor's current position. Pipe writes can be short when the 4096-byte pipe buffer has limited space.", "Returns the byte count, possibly less than length, or -1 and sets errno.", "-EBADF: fd does not refer to an open descriptor.\n-EFAULT: buffer is not accessible for length bytes.\n-EPIPE: The pipe has no readers.\n-EIO: The underlying file or device reports an I/O failure.", "The synchronous libc call is implemented as one OP_WRITE submission on the process AIO ring. O_APPEND is resolved by the kernel."),
    "dup": ("#include <unistd.h>\n\nint dup(int fd);", "Creates the lowest-numbered available descriptor referring to the same open file description as fd.", "Returns the new descriptor, or -1 and sets errno.", "-EBADF: fd does not refer to an open descriptor.\n-EMFILE: The process descriptor table has no free entry.", "The duplicate shares the underlying file position and open status with fd."),
    "dup2": ("#include <unistd.h>\n\nint dup2(int old_fd, int new_fd);", "Makes new_fd refer to the same open file description as old_fd. An existing target is replaced; equal numbers are left unchanged.", "Returns new_fd, or -1 and sets errno.", "-EBADF: old_fd is invalid or new_fd is outside the process descriptor table.", "The requested descriptor number is installed directly."),
    "fcntl": ("#include <fcntl.h>\n\nint fcntl(int fd, int command, ...);", "Manipulates descriptor and open-file-description flags. F_DUPFD and F_DUPFD_CLOEXEC allocate atomically from a requested minimum; F_GETFD and F_SETFD access FD_CLOEXEC; F_GETFL and F_SETFL access O_APPEND and O_NONBLOCK.", "Returns a new descriptor, a flag word, or zero; otherwise -1 and sets errno.", "-EBADF: fd does not refer to an open descriptor.\n-EINVAL: The command, minimum descriptor, or flag word is unsupported.\n-EMFILE: No descriptor is available at or above the requested minimum.", "Descriptor flags are not copied by ordinary duplication. Status flags are shared by aliases of the same open file description; the native operation does not poll or bypass the AIO descriptor layer."),
    "pipe": ("#include <unistd.h>\n\nint pipe(int fds[2]);", "Creates an anonymous pipe and stores its read end in fds[0] and write end in fds[1]. The pipe buffer is 4096 bytes.", "Returns zero, or -1 and sets errno.", "-EFAULT: fds is not writable for two descriptor values.\n-ESRCH: There is no current process.\n-ENOMEM: The pipe object or endpoint descriptors cannot be allocated.", "An empty pipe read waits while a writer remains; closing the last writer completes a waiting read with zero."),
    "stat": ("#include <sys/stat.h>\n\nint stat(const char *path, struct stat *result);", "Obtains metadata for path and writes the Astia libc struct stat. Type, mode, size, block count, timestamps, and the VFS device/inode/link values are supplied.", "Returns zero, or -1 and sets errno.", "-EFAULT: path or result is null or inaccessible.\n-EINVAL: Path resolution fails.\n-ENOENT: The path does not name an object.", "Unsupported metadata fields remain zero rather than being inferred."),
    "fstat": ("#include <sys/stat.h>\n\nint fstat(int fd, struct stat *result);", "Obtains metadata for the open file referred to by fd and writes result.", "Returns zero, or -1 and sets errno.", "-EFAULT: result is null or not writable.\n-EBADF: fd does not refer to an open descriptor.\n-ESPIPE: The underlying object does not provide a file size.", "fstat uses the native STATX fd form."),
    "lstat": ("#include <sys/stat.h>\n\nint lstat(const char *path, struct stat *result);", "Obtains metadata for path and writes the Astia libc struct stat. The final component is not followed when it is a symbolic link, so the link's own type, mode, owner, group, size, timestamps, device, and inode are reported; intermediate symbolic links are resolved normally.", "Returns zero, or -1 and sets errno.", "-EACCES: A directory in path cannot be searched.\n-EFAULT: path or result is null or inaccessible.\n-EINVAL: path is empty.\n-ELOOP: Symbolic link expansion exceeds the native limit.\n-ENAMETOOLONG: A path component or the resolved path is too long.\n-ENOENT: path or an intermediate symbolic link target does not exist.\n-ENOTDIR: An intermediate path component is not a directory.", "lstat submits the native STATX operation with STATX_NOFOLLOW. Unlike stat, a symbolic link in the final position is reported itself, including when its target does not exist."),
    "mkdir": ("#include <sys/stat.h>\n\nint mkdir(const char *path, mode_t mode);", "Creates a directory at path with the supplied permission bits. Parent directories must already exist.", "Returns zero, or -1 and sets errno.", "-EFAULT: path is null or cannot be read.\n-EINVAL: Path resolution fails.\n-EEXIST: The path already exists.\n-ENOENT: The parent directory does not exist.\n-ENOSPC: The filesystem has no free entry.", "Only the low permission bits in mode are retained by tmpfs."),
    "rmdir": ("#include <unistd.h>\n\nint rmdir(const char *path);", "Removes an empty directory at path.", "Returns zero, or -1 and sets errno.", "-EFAULT: path is null or cannot be read.\n-EINVAL: Path resolution fails.\n-ENOENT: The directory does not exist.\n-ENOTDIR: path names a non-directory.\n-ENOTEMPTY: The directory contains an entry.\n-EEXIST: path is the mounted filesystem root.", "This is the native unlink operation with its remove-directory flag."),
    "unlink": ("#include <unistd.h>\n\nint unlink(const char *path);", "Removes a non-directory filesystem entry at path.", "Returns zero, or -1 and sets errno.", "-EFAULT: path is null or cannot be read.\n-EINVAL: Path resolution fails.\n-ENOENT: The path does not exist.\n-EISDIR: path names a directory.\n-EEXIST: path is the mounted filesystem root.", "The native VFS has no separate link-count behavior exposed by this API."),
    "rename": ("#include <unistd.h>\n\nint rename(const char *old_path, const char *new_path);", "Renames old_path to new_path within the same mounted filesystem. An existing target of the same type is replaced.", "Returns zero, or -1 and sets errno.", "-EFAULT: Either pathname cannot be read.\n-EINVAL: Path resolution fails or a directory is moved below itself.\n-ENOENT: A path or parent directory does not exist.\n-ENOTDIR: The source and target types are incompatible.\n-EISDIR: The source and target types are incompatible.\n-ENOTEMPTY: An existing target directory is not empty.\n-EXDEV: The paths are on different mounted filesystems.", "The libc API exposes no rename flags."),
    "link": ("#include <unistd.h>\n\nint link(const char *old_path, const char *new_path);\nint linkat(int olddirfd, const char *old_path, int newdirfd, const char *new_path, int flags);", "Adds a second tmpfs directory entry for an existing regular file. linkat accepts only flags equal to zero.", "Returns zero, or -1 and sets errno.", "-EFAULT: A pathname is invalid or inaccessible.\n-EEXIST: new_path already exists.\n-ENOENT: A pathname or parent directory does not exist.\n-EPERM: The source is a directory or the filesystem does not support hard links.\n-EXDEV: The paths name different mounts.", "Astia hard links share the inode, open file object, data, and link count. The source final symlink is followed; the destination is never replaced. The operation is native tmpfs-only and does not access a host filesystem."),
    "chmod": ("#include <sys/stat.h>\n\nint chmod(const char *path, mode_t mode);", "Changes the permission bits of the filesystem entry at path.", "Returns zero, or -1 and sets errno.", "-EFAULT: path is null or cannot be read.\n-EINVAL: Path resolution fails.\n-ENOENT: The path does not exist.", "The current permission model stores the low 07777 bits and does not claim multi-user authorization semantics."),
    "chown": ("#include <sys/stat.h>\n\nint chown(const char *path, uid_t owner, gid_t group);", "Changes the stored owner UID and group GID of the path's target. The final path component is followed, and an owner or group value of (uid_t) -1 or (gid_t) -1 leaves that field unchanged.", "Returns zero, or -1 and sets errno.", "-EACCES: A directory in path cannot be searched.\n-EFAULT: path is null or cannot be read.\n-EINVAL: owner or group cannot be represented by the direct ABI, or path is empty.\n-ENOENT: The target does not exist.\n-ENOTDIR: An intermediate path component is not a directory.\n-EPERM: The caller is not privileged and either is not the owner, attempts to change the owner to a different UID, or requests a group it does not belong to.\n-EROFS: The filesystem is read-only.", "The kernel permits an effective UID of zero to set any owner and group. A non-root caller must have an effective UID equal to the file's current owner, may leave the owner unchanged or set it to that same value, and may set a group only when that group is its effective GID or a supplementary group. Astia applies the change to the resolved file object."),
    "lchown": ("#include <sys/stat.h>\n\nint lchown(const char *path, uid_t owner, gid_t group);", "Changes the stored owner UID and group GID of the path's final object without following that final component. Intermediate components are still resolved normally.", "Returns zero, or -1 and sets errno.", "-EFAULT: path is null or cannot be read.\n-EINVAL: Path resolution fails.\n-ENOENT: The target does not exist.", "The native path layer preserves the no-follow request for the final component."),
    "fchown": ("#include <sys/stat.h>\n\nint fchown(int fd, uid_t owner, gid_t group);", "Changes the stored owner UID and group GID of the object referred to by fd.", "Returns zero, or -1 and sets errno.", "-EBADF: fd is negative or does not refer to an open descriptor.\n-EINVAL: The descriptor does not refer to a chownable object.", "The descriptor identifies the already opened object, so no pathname or final-component traversal occurs."),
    "access": ("#include <unistd.h>\n\nint access(const char *path, int mode);", "Checks whether the real user and group credentials can use path with the requested F_OK, R_OK, W_OK, and X_OK bits. The final symbolic link is followed. F_OK checks existence and mode zero performs that same existence check.", "Returns zero when every requested check succeeds, or -1 and sets errno.", "-EACCES: A requested read, write, or execute permission bit is absent.\n-EFAULT: path is null or cannot be read.\n-EINVAL: mode contains a bit other than F_OK, R_OK, W_OK, or X_OK.\n-ENOENT: The path does not name an object.\n-ENOTDIR: An intermediate path component is not a directory.", "access calls faccessat(AT_FDCWD, path, mode, 0), so real rather than effective credentials are used. Owner bits are selected for the real UID, group bits for the real GID or any supplementary group, and otherwise other bits. Real UID zero bypasses read and write checks but still requires some execute bit for X_OK. faccessat with AT_EACCESS selects effective credentials instead."),
    "isatty": ("#include <unistd.h>\n\nint isatty(int fd);", "Reports whether fd refers to a terminal. The wrapper probes the descriptor with the TIOCGPGRP ioctl, so any open descriptor backed by a terminal succeeds, not only descriptors 0 through 2.", "Returns 1 when the probe succeeds and 0 otherwise. A failed probe still returns 0 rather than -1.", "-EBADF: fd is negative or does not refer to an open descriptor.\n-ENOTTY: fd does not refer to a terminal.", "The probe leaves errno from the failed ioctl in place when it returns 0. Descriptors for regular files, pipes, and non-terminal devices therefore report zero."),
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
    "getgroups": ("#include <unistd.h>\n\nint getgroups(int count, gid_t groups[]);", "Returns the process supplementary group list. With count zero, groups is not written and the required element count is returned; otherwise count must be at least that large.", "Returns the number of supplementary groups, or -1 and sets errno.", "-EFAULT: groups is not writable for the returned group list.\n-EINVAL: count is negative, greater than 16, or nonzero and smaller than the stored group count.\n-ESRCH: The calling process does not exist.", "The native credential store permits at most 16 supplementary groups. The returned list excludes the effective GID unless it is also present in that list."),
    "setgroups": ("#include <unistd.h>\n\nint setgroups(int count, const gid_t groups[]);", "Replaces the process supplementary group list with count values from groups.", "Returns zero, or -1 and sets errno.", "-EFAULT: groups is not readable for count group IDs.\n-EINVAL: count is negative or greater than 16.\n-EPERM: The current effective UID is not zero.\n-ESRCH: The calling process does not exist.", "An empty list is valid and does not dereference groups. The kernel copies the list before replacing the process credential state."),
    "waitpid": ("#include <sys/wait.h>\n\nint waitpid(int pid, int *status, int options);", "Waits for a selected child and translates the direct waitpid result into the libc −1 and errno convention. Positive pid selects one child; zero selects the caller's process group; −1 selects any child; and a negative value selects a process group.", "Returns the reaped or reported child PID, zero only with WNOHANG when no event is ready, or -1 and sets errno.", "-EFAULT: status is non‑null but not writable.\n-ECHILD: No matching child exists.\n-EINVAL: pid or options is invalid.\n-ESRCH: The calling process or thread does not exist.", "The only accepted flags are WNOHANG, WUNTRACED, and WCONTINUED. WIFEXITED, WEXITSTATUS, WIFSIGNALED, WTERMSIG, WIFSTOPPED, WSTOPSIG, and WIFCONTINUED interpret the status word emitted by the kernel."),
    "wait": ("#include <sys/wait.h>\n\nint wait(int *status);", "Waits for and reaps any child process, using the same status encoding as waitpid.", "Returns the child PID, or -1 and sets errno.", "-EFAULT: status is non‑null but not writable.\n-ECHILD: The caller has no children.", "This is equivalent to waitpid(-1, status, 0)."),
    "getsid": ("#include <unistd.h>\n\npid_t getsid(pid_t pid);", "Returns the session ID of the selected process. pid zero selects the caller; a positive pid selects that process, including a zombie that has not yet been reaped.", "Returns the session ID, or -1 and sets errno.", "-ESRCH: pid is negative, cannot be represented by the native PID domain, or names no registered process.", "Astia has no cross-process permission restriction for this query. A process remains selectable until its parent reaps it with waitpid."),
    "execl": ("#include <unistd.h>\n\nint execl(const char *path, const char *arg0, ...);", "Replaces the calling image with path, constructing argv from the NULL-terminated argument list and inheriting environ.", "Does not return on success; returns -1 and sets errno on failure.", "-E2BIG: The argument or environment vector exceeds its limit.\n-EFAULT: A path or argument pointer is invalid.\n-ENOENT: path does not exist.\n-ENOEXEC: path is not a valid executable.\n-ENOMEM: The replacement image cannot be allocated.", "The final variadic argument must be a null pointer."),
    "execle": ("#include <unistd.h>\n\nint execle(const char *path, const char *arg0, ...);", "Replaces the calling image with path, constructing argv up to a null pointer and taking the environment vector following that terminator.", "Does not return on success; returns -1 and sets errno on failure.", "-E2BIG: The argument or environment vector exceeds its limit.\n-EFAULT: A path, argument, or environment pointer is invalid.\n-ENOENT: path does not exist.\n-ENOEXEC: path is not a valid executable.\n-ENOMEM: The replacement image cannot be allocated.", "The argument list must contain a null pointer before the final environment-vector argument."),
    "execlp": ("#include <unistd.h>\n\nint execlp(const char *file, const char *arg0, ...);", "Searches PATH for file and replaces the calling image, constructing argv from the NULL-terminated argument list and inheriting environ.", "Does not return on success; returns -1 and sets errno on failure.", "-E2BIG: The argument or environment vector exceeds its limit.\n-EFAULT: An argument pointer is invalid.\n-ENOENT: No PATH entry names file.\n-ENOEXEC: A selected file is not a valid executable.\n-ENOMEM: The replacement image cannot be allocated.", "The final variadic argument must be a null pointer."),
    "utime": ("#include <sys/stat.h>\n\nint utime(const char *path, const time_t *value);", "Updates path's access and modification times from value, or to the current kernel realtime when value is null.", "Returns zero, or -1 and sets errno.", "-EACCES: A current-time update requires write access and the caller is neither the owner nor privileged.\n-EFAULT: path is null or cannot be read.\n-EINVAL: Path resolution fails.\n-ENOENT: The path does not exist.\n-EPERM: An explicit timestamp was supplied by a caller that is neither the owner nor privileged.\n-EROFS: The filesystem does not support timestamp updates.", "Implemented as utimensat(AT_FDCWD, path, times, 0), converting the struct utimbuf values into the POSIX times array."),
    "utimensat": ("#include <sys/stat.h>\n\nint utimensat(int dirfd, const char *path, const struct timespec times[2], int flags);", "Updates path's access and modification times. times is either null (both are set to the current kernel realtime) or a two-element array whose entries may use UTIME_NOW or UTIME_OMIT. Relative paths are resolved against dirfd, and AT_SYMLINK_NOFOLLOW updates a symbolic link instead of its target.", "Returns zero, or -1 and sets errno.", "-EACCES: A current-time update requires write access and the caller is neither the owner nor privileged.\n-EFAULT: path or times cannot be read.\n-EINVAL: flags contains an unsupported bit, a nanosecond field is out of range, or path resolution fails.\n-ENOENT: The path does not exist.\n-EPERM: An explicit timestamp was supplied by a caller that is neither the owner nor privileged.\n-EROFS: The filesystem does not support timestamp updates.", "When both entries are UTIME_OMIT the call succeeds without modifying either timestamp or requiring access to the final node. dirfd may be AT_FDCWD or an open directory descriptor."),
    "file_permissions": ("#include <astia/fs.hpp>\n\nclass file_permissions;", "Represents the low 12 permission bits of a kernel filesystem mode. Construction masks the value to 07777; raw(), owner(), group(), and other() expose the stored value and its three permission fields. owner_allows(), group_allows(), and other_allows() test read, write, or execute bits.", "The constexpr accessors return the requested permission value or Boolean decision.", "No runtime errors are produced by this value type.", "This is a kernel C++ API, not a POSIX mode_t object. It does not carry the file type bits."),
    "file_mode": ("#include <astia/fs.hpp>\n\nclass file_mode;", "Combines a file_type with file_permissions. It identifies unknown, FIFO, character, directory, regular, and symlink nodes; exists() reports whether the type is not unknown. chmod() returns a mode with the same type and the supplied permission bits masked to 07777.", "The constexpr queries return the stored type, permissions, existence state, or permission decision.", "No runtime errors are produced by this value type.", "The type and permission components are deliberately separate. chmod() changes neither ownership nor file type."),
    "tcgetpgrp": ("#include <unistd.h>\n\npid_t tcgetpgrp(int fd);", "Returns the foreground process group associated with the terminal referred to by fd.", "Returns the foreground process-group ID, or -1 and sets errno.", "-EBADF: fd does not refer to an open descriptor.\n-EFAULT: The kernel cannot access the output argument.\n-ENOTTY: fd is not a terminal.", "The wrapper uses TIOCGPGRP. A terminal opened through /dev/tty resolves to the calling process's controlling terminal."),
    "tcsetpgrp": ("#include <unistd.h>\n\nint tcsetpgrp(int fd, int pgrp);", "Makes pgrp the foreground process group of the terminal referred to by fd.", "Returns zero, or -1 and sets errno.", "-EBADF: fd does not refer to an open descriptor.\n-EPERM: pgrp is nonpositive or is not a process group in the caller's session.\n-ENOTTY: fd is not a terminal.", "The wrapper uses TIOCGPGRP/TIOCSPGRP through ioctl. The target group must exist in the caller's session."),
    "devfs": ("#include <fcntl.h>\n\n/* mounted at /dev */", "Astia mounts the fixed devfs instance at /dev. It exposes registered character devices, including /dev/null and /dev/zero, and the dynamic /dev/tty node. /dev/tty exists for metadata lookup even when the caller has no controlling terminal, but opening it fails unless the caller has one. Device entries use character type and their registered permission mode.", "There is no callable devfs function in libc; applications use the ordinary open, read, write, and ioctl interfaces.", "-EACCES: The requested descriptor access is not allowed by the node's other permission bits.\n-ENOENT: The requested device entry is not registered or /dev/tty has no controlling terminal.", "The devfs registry holds at most 32 exact absolute paths. /dev/null discards writes and returns end of file on reads; /dev/zero fills reads with zero bytes."),
}


def libc_pages() -> list[tuple[str, str, str, int]]:
    return [(name, f"Synopsis:\n    {synopsis}\n\nDescription:\n{description}\n\nReturn Value:\n{returns}\n\nErrors:\n{errors}\n\nNotes:\n{notes}", "libc/filesystem", 3) for name, (synopsis, description, returns, errors, notes) in LIBC_PAGES.items()]


def markdown_inline(text: str) -> str:
    escaped = html.escape(text, quote=False)
    return re.sub(r"`([^`\n]+)`", r"<code>\1</code>", escaped)


def markdown_page(text: str, fallback_title: str) -> tuple[str, str]:
    lines = text.splitlines()
    title = fallback_title.replace("-", " ").replace("_", " ").title()
    rendered: list[str] = []
    paragraph: list[str] = []
    list_kind: str | None = None
    list_items: list[str] = []
    fence: list[str] | None = None
    fence_language = ""
    first_heading = True

    def flush_paragraph() -> None:
        if paragraph:
            rendered.append(f"<p>{markdown_inline(' '.join(paragraph))}</p>")
            paragraph.clear()

    def flush_list() -> None:
        nonlocal list_kind
        if list_kind is not None:
            items = "".join(f"<li>{item}</li>" for item in list_items)
            rendered.append(f"<{list_kind}>{items}</{list_kind}>")
            list_items.clear()
            list_kind = None

    for line in lines:
        if fence is not None:
            if re.match(r"^\s*```", line):
                language = f' class="language-{html.escape(fence_language, quote=True)}"' if fence_language else ""
                code = html.escape("\n".join(fence), quote=False)
                rendered.append(f"<pre><code{language}>{code}</code></pre>")
                fence = None
                fence_language = ""
            else:
                fence.append(line)
            continue

        fence_match = re.match(r"^\s*```\s*([\w+-]*)\s*$", line)
        if fence_match:
            flush_paragraph()
            flush_list()
            fence = []
            fence_language = fence_match.group(1)
            continue

        heading_match = re.match(r"^\s*(#{1,6})\s+(.+?)\s*#*\s*$", line)
        if heading_match:
            flush_paragraph()
            flush_list()
            level = len(heading_match.group(1))
            heading = heading_match.group(2)
            if first_heading and level == 1:
                title = heading
                first_heading = False
                continue
            first_heading = False
            rendered.append(f"<h{level}>{markdown_inline(heading)}</h{level}>")
            continue

        item_match = re.match(r"^\s*([-+*]|\d+[.)])\s+(.+?)\s*$", line)
        if item_match:
            flush_paragraph()
            kind = "ol" if item_match.group(1)[0].isdigit() else "ul"
            if list_kind != kind:
                flush_list()
                list_kind = kind
            list_items.append(markdown_inline(item_match.group(2)))
            continue

        if list_kind is not None and list_items and re.match(r"^\s{2,}\S", line):
            list_items[-1] += " " + markdown_inline(line.strip())
            continue

        if not line.strip():
            flush_paragraph()
            flush_list()
            continue

        flush_list()
        paragraph.append(line.strip())

    if fence is not None:
        code = html.escape("\n".join(fence), quote=False)
        rendered.append(f"<pre><code>{code}</code></pre>")
    flush_paragraph()
    flush_list()
    return title, "".join(rendered)


def overview_pages(records: tuple[dict, ...]) -> list[tuple[str, str, str]]:
    pages = []
    for record in records:
        title, body = markdown_page(record["markdown"], record["name"])
        pages.append((record["name"], title, body))
    return pages


def stylesheet_href(page_name: str) -> str:
    return posixpath.relpath("nodivia.css", posixpath.dirname(page_name) or ".")


def render_overview(name: str, title: str, body: str, stylesheet: str) -> str:
    page_title = f"{name}(7)"
    return f"""<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>{html.escape(page_title)} - Astia manual</title>
<link rel="stylesheet" href="{html.escape(stylesheet, quote=True)}"></head>
<body><main class="surface surface__flex">
<article class="surface__level__1 surface__minor">
<h1>{html.escape(page_title)}</h1>
<nav><a href="index.html">Astia manual index</a></nav>
{body}
</article></main></body></html>
"""


def render_markdown_page(name: str, markdown: str, section: int, stylesheet: str) -> str:
    _, body = markdown_page(markdown, name)
    page_title = f"{name}({section})"
    return f"""<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>{html.escape(page_title)} - Astia manual</title>
<link rel="stylesheet" href="{html.escape(stylesheet, quote=True)}"></head>
<body><main class="surface surface__flex">
<article class="surface__level__1 surface__minor">
<h1>{html.escape(page_title)}</h1>
<nav><a href="index.html">Astia manual index</a></nav>
{body}
</article></main></body></html>
"""


def body_sections(body: str) -> dict[str, list[str]]:
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
    return values


def error_entries(content: str) -> list[tuple[str, str]] | None:
    matches = list(re.finditer(r"(?<!\w)(-?E[A-Z0-9]+)\s*:\s*", content))
    if not matches:
        return None
    entries = []
    for index, match in enumerate(matches):
        code = match.group(1)
        end = matches[index + 1].start() if index + 1 < len(matches) else len(content)
        reason = " ".join(content[match.end():end].split()).strip().rstrip(" .;")
        reason = reason or ERROR_FALLBACKS.get(code.lstrip("-"), "the operation failed")
        entries.append((code, sentence(reason)))
    return entries


def markdown_content(content: str) -> str:
    lines = content.splitlines()
    return "\n".join(line[4:] if line.startswith("    ") else line for line in lines).strip()


def render_syscall_markdown(
    name: str, body: str, section: int, raw_abi: bool = False
) -> str:
    values = body_sections(body)

    def content_for(key: str) -> str:
        return markdown_content("\n".join(values.get(key, [])))

    parts = [f"# {name}({section})"]
    synopsis = content_for("SYNOPSIS")
    if raw_abi:
        parts.extend(
            [
                "## SYNOPSIS",
                "Raw kernel ABI. No public C wrapper declaration is provided for this syscall.",
            ]
        )
    elif synopsis:
        parts.extend(["## SYNOPSIS", f"```c\n{synopsis}\n```"])
    description = content_for("DESCRIPTION")
    if description:
        parts.extend(["## DESCRIPTION", description])
    returns = content_for("RETURN VALUE")
    if returns:
        parts.extend(["## RETURN VALUE", returns])
    errors = content_for("ERRORS")
    if errors:
        entries = error_entries(errors)
        if entries is None:
            parts.extend(["## ERRORS", errors])
        else:
            parts.extend(
                [
                    "## ERRORS",
                    "\n".join(f"- `{code}`: {reason}" for code, reason in entries),
                ]
            )
    notes = content_for("NOTES")
    if notes:
        parts.extend(["## NOTES", notes])
    return "\n\n".join(parts).strip().replace("\u2014", " - ") + "\n"


CANONICAL_PUBLIC_SYSCALL_IDS = tuple(range(81))


def syscall_abi(path: Path) -> list[tuple[int, str]]:
    entries = []
    for line in path.read_text().splitlines():
        match = re.match(r"\s*SYS_([A-Z0-9_]+)\s*=\s*(\d+)\s*,", line)
        if match:
            entries.append((int(match.group(2)), match.group(1).lower()))
    entries.sort()
    if [value for value, _ in entries] != list(CANONICAL_PUBLIC_SYSCALL_IDS):
        raise ValueError(
            f"{path}: expected canonical section-2 syscalls at IDs "
            f"{list(CANONICAL_PUBLIC_SYSCALL_IDS)}, found {entries}"
        )
    return entries


def public_abi_prototypes(abi_header: Path) -> dict[str, str]:
    prototypes: dict[str, str] = {}
    pattern = re.compile(
        r"static inline\s+([^;{}]*?\bastia_([a-z0-9_]+)\s*\([^;{}]*\))\s*\{",
        re.S,
    )
    for path in sorted(abi_header.parent.glob("*.h")):
        for match in pattern.finditer(path.read_text(errors="ignore")):
            prototype = " ".join(match.group(1).split())
            prototypes[match.group(2)] = prototype + ";"
    return prototypes


def group_for_source(source: Path | None, source_root: Path) -> str:
    if source is None:
        return "abi"
    try:
        parts = source.resolve().relative_to(source_root.resolve()).parts
    except ValueError:
        return "abi"
    if not parts:
        return "abi"
    root = parts[0]
    if len(parts) >= 3 and parts[1] == "syscalls":
        return f"{root}/{parts[2]}" if len(parts) > 3 else root
    return root


def taxonomy_for_group(group: str) -> tuple[str, str]:
    root, _, child = group.partition("/")
    if root == "proc":
        return root, child or "core"
    return root, root


def taxonomy_group(subsystem: str, category: str) -> str:
    return f"{subsystem}/{category}" if subsystem == "proc" else subsystem


def authored_source_path(name: str, subsystem: str, category: str) -> str:
    suffix = f"{category}/{name}.md" if subsystem == "proc" else f"{name}.md"
    return f"docs/2/{subsystem}/{suffix}"


GENERATED_SYSCALL_ASSET = re.compile(
    r"^syscall-(0|[1-9][0-9]?)-[a-z0-9]+(?:-[a-z0-9]+)*\.md$"
)


def generated_syscall_asset(name: str) -> bool:
    if Path(name).name != name:
        return False
    match = GENERATED_SYSCALL_ASSET.fullmatch(name)
    return bool(
        match and int(match.group(1)) in CANONICAL_PUBLIC_SYSCALL_IDS
    )


def validated_output_name(name: str) -> str:
    if not name or name in {".", ".."} or Path(name).name != name:
        raise ValueError(f"unsafe output name: {name!r}")
    return name


def validated_relative_path(name: str) -> tuple[str, ...]:
    path = Path(name)
    if path.is_absolute() or not path.parts or any(
        part in {"", ".", ".."} for part in path.parts
    ):
        raise ValueError(f"unsafe output path: {name!r}")
    return path.parts


def open_directory_chain(path: Path, trusted_root: Path) -> int:
    root = Path(os.path.abspath(trusted_root))
    target = Path(os.path.abspath(path))
    try:
        relative = target.relative_to(root)
    except ValueError as error:
        raise ValueError(f"path escapes trusted root: {path}") from error
    try:
        descriptor = os.open(root, os.O_RDONLY | os.O_DIRECTORY)
    except OSError as error:
        raise ValueError(f"trusted root is not a directory: {trusted_root}") from error
    try:
        for part in relative.parts:
            try:
                os.mkdir(part, 0o755, dir_fd=descriptor)
            except FileExistsError:
                pass
            try:
                child = os.open(
                    part,
                    os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW,
                    dir_fd=descriptor,
                )
            except OSError as error:
                raise ValueError(
                    f"path has a symlink or non-directory component: {part}"
                ) from error
            os.close(descriptor)
            descriptor = child
        return descriptor
    except BaseException:
        os.close(descriptor)
        raise


def same_inode(first: os.stat_result, second: os.stat_result) -> bool:
    return (first.st_dev, first.st_ino) == (second.st_dev, second.st_ino)


class OutputDirectory:
    def __init__(self, path: Path, descriptor: int, owns_descriptor: bool = True):
        self.path = path
        self._descriptor = descriptor
        self._owns_descriptor = owns_descriptor

    @classmethod
    def open(cls, output_dir: Path, trusted_root: Path) -> "OutputDirectory":
        return cls(
            Path(os.path.abspath(output_dir)),
            open_directory_chain(output_dir, trusted_root),
        )

    def __enter__(self) -> "OutputDirectory":
        return self

    def __exit__(self, *_: object) -> None:
        self.close()

    def close(self) -> None:
        if self._descriptor >= 0 and self._owns_descriptor:
            os.close(self._descriptor)
            self._descriptor = -1

    def sync(self) -> None:
        os.fsync(self._descriptor)

    def write_atomic(self, name: str, content: str) -> None:
        validated_output_name(name)
        for _ in range(100):
            temporary = f".{name}.{os.urandom(8).hex()}.tmp"
            try:
                descriptor = os.open(
                    temporary,
                    os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW,
                    0o600,
                    dir_fd=self._descriptor,
                )
                break
            except FileExistsError:
                continue
        else:
            raise OSError(f"cannot allocate a temporary output for {name}")
        try:
            with os.fdopen(descriptor, "w", encoding="utf-8") as output:
                output.write(content)
                output.flush()
                os.fsync(output.fileno())
                os.fchmod(output.fileno(), 0o644)
            os.replace(
                temporary,
                name,
                src_dir_fd=self._descriptor,
                dst_dir_fd=self._descriptor,
            )
        except BaseException:
            try:
                os.unlink(temporary, dir_fd=self._descriptor)
            except FileNotFoundError:
                pass
            raise

    def read_text(self, name: str) -> str | None:
        validated_output_name(name)
        try:
            descriptor = os.open(
                name,
                os.O_RDONLY | os.O_NONBLOCK | os.O_NOFOLLOW,
                dir_fd=self._descriptor,
            )
        except OSError:
            return None
        try:
            info = os.fstat(descriptor)
            if not stat.S_ISREG(info.st_mode):
                return None
            source = os.fdopen(descriptor, "r", encoding="utf-8")
            descriptor = -1
            with source:
                return source.read()
        except (OSError, UnicodeDecodeError):
            return None
        finally:
            if descriptor >= 0:
                os.close(descriptor)


    def read_manifest(self, name: str) -> str | None:
        validated_output_name(name)
        try:
            descriptor = os.open(
                name,
                os.O_RDONLY | os.O_NONBLOCK | os.O_NOFOLLOW,
                dir_fd=self._descriptor,
            )
        except FileNotFoundError:
            return None
        except OSError as error:
            raise ValueError(
                f"manifest is not a readable regular file: {name}"
            ) from error
        try:
            info = os.fstat(descriptor)
            if not stat.S_ISREG(info.st_mode):
                raise ValueError(f"manifest is not a regular file: {name}")
            source = os.fdopen(descriptor, "r", encoding="utf-8")
            descriptor = -1
            with source:
                return source.read()
        except UnicodeDecodeError as error:
            raise ValueError(f"manifest is not valid UTF-8: {name}") from error
        finally:
            if descriptor >= 0:
                os.close(descriptor)


def _unsupported_xattrs(error: OSError) -> bool:
    return error.errno in {
        errno.ENOTSUP,
        getattr(errno, "EOPNOTSUPP", errno.ENOTSUP),
        errno.ENOSYS,
    }


def fd_path(descriptor: int, name: str | None = None) -> str:
    base = PROC_FD_ROOT / str(descriptor)
    return str(base if name is None else base / name)


def read_xattrs(descriptor: int, name: str | None) -> tuple[tuple[str, bytes], ...]:
    path = fd_path(descriptor, name)
    follow = name is None
    try:
        attributes = os.listxattr(path, follow_symlinks=follow)
    except OSError as error:
        if _unsupported_xattrs(error):
            return ()
        raise
    values = []
    for attribute in sorted(attributes):
        values.append(
            (
                attribute,
                os.getxattr(path, attribute, follow_symlinks=follow),
            )
        )
    return tuple(values)


def clear_xattrs(descriptor: int, name: str | None, relative: str) -> None:
    path = fd_path(descriptor, name)
    follow = name is None
    try:
        attributes = os.listxattr(path, follow_symlinks=follow)
    except OSError as error:
        if _unsupported_xattrs(error):
            return
        raise ValueError(
            f"cannot read destination extended attributes: {relative}"
        ) from error
    for attribute in attributes:
        try:
            os.removexattr(path, attribute, follow_symlinks=follow)
        except OSError as error:
            if error.errno == errno.ENODATA:
                continue
            raise ValueError(
                f"cannot remove destination extended attribute {attribute}: {relative}"
            ) from error


def copy_xattrs(
    source: int,
    destination: int,
    name: str | None,
    relative: str,
) -> None:
    source_path = fd_path(source, name)
    destination_path = fd_path(destination, name)
    follow_source = name is None
    try:
        attributes = os.listxattr(source_path, follow_symlinks=follow_source)
    except OSError as error:
        if not _unsupported_xattrs(error):
            raise ValueError(
                f"cannot read extended attributes: {relative}"
            ) from error
        try:
            os.listxattr(destination_path, follow_symlinks=follow_source)
        except OSError as destination_error:
            if _unsupported_xattrs(destination_error):
                return
            raise ValueError(
                f"cannot read destination extended attributes: {relative}"
            ) from destination_error
        raise ValueError(
            f"source filesystem does not support extended attributes: {relative}"
        ) from error
    for attribute in attributes:
        try:
            value = os.getxattr(
                source_path, attribute, follow_symlinks=follow_source
            )
        except OSError as error:
            raise ValueError(
                f"cannot read extended attribute {attribute}: {relative}"
            ) from error
        try:
            os.setxattr(
                destination_path,
                attribute,
                value,
                flags=0,
                follow_symlinks=follow_source,
            )
        except OSError as error:
            raise ValueError(
                f"cannot preserve extended attribute {attribute}: {relative}"
            ) from error


def replace_xattrs(
    source: int,
    destination: int,
    name: str | None,
    relative: str,
) -> None:
    clear_xattrs(destination, name, relative)
    copy_xattrs(source, destination, name, relative)


def copy_unowned_entries(
    source: int,
    destination: int,
    owned: set[str],
    manifest_name: str,
    prefix: str = "",
) -> None:
    for name in os.listdir(source):
        relative = f"{prefix}/{name}" if prefix else name
        if not prefix and name == manifest_name:
            continue
        info = os.stat(name, dir_fd=source, follow_symlinks=False)
        if stat.S_ISDIR(info.st_mode):
            os.mkdir(name, stat.S_IMODE(info.st_mode), dir_fd=destination)
            try:
                os.chown(
                    name,
                    info.st_uid,
                    info.st_gid,
                    dir_fd=destination,
                    follow_symlinks=False,
                )
            except OSError as error:
                raise ValueError(
                    f"cannot preserve directory ownership: {relative}"
                ) from error
            os.chmod(name, stat.S_IMODE(info.st_mode), dir_fd=destination)
            replace_xattrs(source, destination, name, relative)
            with ExitStack() as stack:
                child_source = os.open(
                    name,
                    os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW,
                    dir_fd=source,
                )
                stack.callback(os.close, child_source)
                child_destination = os.open(
                    name,
                    os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW,
                    dir_fd=destination,
                )
                stack.callback(os.close, child_destination)
                copy_unowned_entries(
                    child_source,
                    child_destination,
                    owned,
                    manifest_name,
                    relative,
                )
        elif relative in owned:
            if not (stat.S_ISREG(info.st_mode) or stat.S_ISLNK(info.st_mode)):
                raise ValueError(
                    f"owned output entry has an unsupported type: {relative}"
                )
            continue
        elif stat.S_ISLNK(info.st_mode):
            os.symlink(os.readlink(name, dir_fd=source), name, dir_fd=destination)
        elif stat.S_ISREG(info.st_mode):
            with ExitStack() as stack:
                source_fd = os.open(
                    name,
                    os.O_RDONLY | os.O_NONBLOCK | os.O_NOFOLLOW,
                    dir_fd=source,
                )
                stack.callback(os.close, source_fd)
                try:
                    source_info = os.fstat(source_fd)
                    if not stat.S_ISREG(source_info.st_mode):
                        raise ValueError(
                            f"unowned output entry changed type during copy: {relative}"
                        )
                except OSError as error:
                    raise ValueError(
                        f"cannot inspect unowned output entry: {relative}"
                    ) from error
                destination_fd = os.open(
                    name,
                    os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW,
                    stat.S_IMODE(info.st_mode),
                    dir_fd=destination,
                )
                stack.callback(os.close, destination_fd)
                while True:
                    block = os.read(source_fd, 65536)
                    if not block:
                        break
                    offset = 0
                    while offset < len(block):
                        written = os.write(destination_fd, block[offset:])
                        if written <= 0:
                            raise OSError("short write while copying output")
                        offset += written
                try:
                    os.fchown(destination_fd, info.st_uid, info.st_gid)
                except OSError as error:
                    raise ValueError(
                        f"cannot preserve file ownership: {relative}"
                    ) from error
                os.fchmod(destination_fd, stat.S_IMODE(info.st_mode))
                replace_xattrs(source, destination, name, relative)
                os.fsync(destination_fd)
        elif stat.S_ISFIFO(info.st_mode):
            os.mkfifo(name, stat.S_IMODE(info.st_mode), dir_fd=destination)
            try:
                os.chown(
                    name,
                    info.st_uid,
                    info.st_gid,
                    dir_fd=destination,
                    follow_symlinks=False,
                )
            except OSError as error:
                raise ValueError(
                    f"cannot preserve FIFO ownership: {relative}"
                ) from error
            os.chmod(name, stat.S_IMODE(info.st_mode), dir_fd=destination)
            replace_xattrs(source, destination, name, relative)
        elif stat.S_ISCHR(info.st_mode) or stat.S_ISBLK(info.st_mode):
            raise ValueError(f"cannot preserve device node: {relative}")
        else:
            raise ValueError(
                f"cannot preserve unsupported filesystem entry: {relative}"
            )


def remove_tree(parent: int, name: str) -> None:
    info = os.stat(name, dir_fd=parent, follow_symlinks=False)
    if not stat.S_ISDIR(info.st_mode):
        if (
            stat.S_ISREG(info.st_mode)
            or stat.S_ISLNK(info.st_mode)
            or stat.S_ISFIFO(info.st_mode)
            or stat.S_ISCHR(info.st_mode)
            or stat.S_ISBLK(info.st_mode)
        ):
            os.unlink(name, dir_fd=parent)
            return
        raise OSError(errno.EPERM, f"refusing to remove unsupported entry: {name}")
    descriptor = os.open(
        name, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW, dir_fd=parent
    )
    try:
        for child in os.listdir(descriptor):
            remove_tree(descriptor, child)
    finally:
        os.close(descriptor)
    os.rmdir(name, dir_fd=parent)


RENAME_NOREPLACE = 1
RENAME_EXCHANGE = 2
_LIBC = ctypes.CDLL(None, use_errno=True)
_RENAMEAT2 = getattr(_LIBC, "renameat2", None)
if _RENAMEAT2 is not None:
    _RENAMEAT2.argtypes = [
        ctypes.c_int,
        ctypes.c_char_p,
        ctypes.c_int,
        ctypes.c_char_p,
        ctypes.c_uint,
    ]
    _RENAMEAT2.restype = ctypes.c_int


def renameat2(parent: int, first: str, second: str, flags: int) -> None:
    if _RENAMEAT2 is None:
        raise OSError(errno.ENOSYS, "renameat2 is unavailable")
    result = _RENAMEAT2(
        parent,
        os.fsencode(first),
        parent,
        os.fsencode(second),
        flags,
    )
    if result != 0:
        error = ctypes.get_errno()
        raise OSError(error, os.strerror(error))


def exchange_directories(parent: int, first: str, second: str) -> None:
    renameat2(parent, first, second, RENAME_EXCHANGE)


def rename_noreplace(parent: int, first: str, second: str) -> None:
    renameat2(parent, first, second, RENAME_NOREPLACE)


def _content_hash(descriptor: int, name: str) -> bytes:
    try:
        source = os.open(
            name,
            os.O_RDONLY | os.O_NONBLOCK | os.O_NOFOLLOW,
            dir_fd=descriptor,
        )
    except OSError as error:
        raise RuntimeError(
            f"cannot open active output file for hashing: {name}: {error}"
        ) from error
    try:
        try:
            info = os.fstat(source)
            if not stat.S_ISREG(info.st_mode):
                raise RuntimeError(
                    f"active output entry changed type during hashing: {name}"
                )
            digest = hashlib.sha256()
            while True:
                block = os.read(source, 65536)
                if not block:
                    break
                digest.update(block)
            return digest.digest()
        except OSError as error:
            raise RuntimeError(
                f"cannot read active output file for hashing: {name}: {error}"
            ) from error
    finally:
        os.close(source)


def _entry_metadata(
    descriptor: int, name: str | None
) -> tuple[int, int, int, int, int, int, int, tuple[tuple[str, bytes], ...], bytes | None]:
    if name is None:
        info = os.fstat(descriptor)
        xattrs = read_xattrs(descriptor, None)
        content_hash = None
    else:
        info = os.stat(name, dir_fd=descriptor, follow_symlinks=False)
        xattrs = read_xattrs(descriptor, name)
        content_hash = (
            _content_hash(descriptor, name)
            if stat.S_ISREG(info.st_mode)
            else None
        )
    return (
        info.st_dev,
        info.st_ino,
        info.st_mode,
        info.st_uid,
        info.st_gid,
        info.st_size,
        info.st_mtime_ns,
        info.st_ctime_ns,
        xattrs,
        content_hash,
    )


def retirement_snapshots_match(
    current: dict[str, tuple], original: dict[str, tuple]
) -> bool:
    if set(current) != set(original):
        return False
    for relative, metadata in current.items():
        expected = original[relative]
        if relative == ".":
            if metadata[:7] + metadata[8:] != expected[:7] + expected[8:]:
                return False
        elif metadata != expected:
            return False
    return True


def directory_snapshot(directory: int, prefix: str = "") -> dict[str, tuple]:
    snapshot: dict[str, tuple] = {}
    if not prefix:
        snapshot["."] = _entry_metadata(directory, None)
    for name in os.listdir(directory):
        relative = f"{prefix}/{name}" if prefix else name
        info = os.stat(name, dir_fd=directory, follow_symlinks=False)
        snapshot[relative] = _entry_metadata(directory, name)
        if stat.S_ISDIR(info.st_mode):
            child = os.open(
                name, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW, dir_fd=directory
            )
            try:
                snapshot.update(directory_snapshot(child, relative))
            finally:
                os.close(child)
    return snapshot


def lexical_path(base: Path, name: str) -> str:
    return os.path.normpath(os.path.join(os.path.abspath(base), name))


def clear_directory(descriptor: int) -> None:
    for child in os.listdir(descriptor):
        info = os.stat(child, dir_fd=descriptor, follow_symlinks=False)
        if stat.S_ISDIR(info.st_mode):
            child_descriptor = os.open(
                child, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW, dir_fd=descriptor
            )
            try:
                clear_directory(child_descriptor)
            finally:
                os.close(child_descriptor)
            os.rmdir(child, dir_fd=descriptor)
        elif (
            stat.S_ISREG(info.st_mode)
            or stat.S_ISLNK(info.st_mode)
            or stat.S_ISFIFO(info.st_mode)
            or stat.S_ISCHR(info.st_mode)
            or stat.S_ISBLK(info.st_mode)
        ):
            os.unlink(child, dir_fd=descriptor)
        else:
            raise OSError(
                errno.EPERM, f"refusing to remove unsupported entry: {child}"
            )


def quarantine_and_remove(
    parent: int,
    name: str,
    expected: os.stat_result,
    descriptor: int,
    prefix: str,
) -> tuple[bool, str, Exception | None]:
    quarantine_name = ""
    for _ in range(100):
        try:
            candidate = f"{prefix}{os.urandom(8).hex()}"
        except Exception as error:
            return False, name, error
        try:
            rename_noreplace(parent, name, candidate)
        except FileExistsError:
            continue
        except Exception as error:
            return False, name, error
        quarantine_name = candidate
        break
    else:
        return False, name, OSError("cannot allocate a quarantine name")
    try:
        info = os.stat(quarantine_name, dir_fd=parent, follow_symlinks=False)
        if not stat.S_ISDIR(info.st_mode) or not same_inode(info, expected):
            return False, quarantine_name, RuntimeError(
                "quarantine identity changed before cleanup"
            )
        clear_directory(descriptor)
        final = os.stat(quarantine_name, dir_fd=parent, follow_symlinks=False)
        if not stat.S_ISDIR(final.st_mode) or not same_inode(final, expected):
            return False, quarantine_name, RuntimeError(
                "quarantine identity changed during cleanup"
            )
        os.rmdir(quarantine_name, dir_fd=parent)
        return True, "", None
    except Exception as error:
        return False, quarantine_name, error


class TransactionalDirectory:
    def __init__(
        self,
        path: Path,
        parent: int,
        active_name: str,
        active: int,
        active_info: os.stat_result | None,
        active_snapshot: dict[str, tuple],
        staging_name: str,
        staging_info: os.stat_result,
        staging: int,
    ):
        self.path = path
        self._parent = parent
        self._active_name = active_name
        self._active = active
        self._active_info = active_info
        self._active_snapshot = active_snapshot
        self._staging_name = staging_name
        self._staging_info = staging_info
        self._staging = staging
        self._committed = False
        self.output = OutputDirectory(path, staging, owns_descriptor=False)

    @classmethod
    def begin(
        cls,
        output_dir: Path,
        trusted_root: Path,
        manifest_name: str,
        owned_reader,
    ) -> "TransactionalDirectory":
        if not PROC_FD_ROOT.is_dir():
            raise ValueError(
                "transactional output requires /proc/self/fd metadata access"
            )
        path = Path(os.path.abspath(output_dir))
        validated_output_name(path.name)
        parent = open_directory_chain(path.parent, trusted_root)
        active = -1
        staging = -1
        staging_name = ""
        staging_created = False
        staging_info = None
        try:
            active_info = None
            active_snapshot: dict[str, tuple] = {}
            try:
                active = os.open(
                    path.name,
                    os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW,
                    dir_fd=parent,
                )
            except FileNotFoundError:
                active = -1
            except OSError as error:
                raise ValueError(
                    f"output directory is a symlink or not a directory: {output_dir}"
                ) from error
            if active >= 0:
                try:
                    active_info = os.fstat(active)
                    active_snapshot = directory_snapshot(active)
                except FileNotFoundError as error:
                    raise RuntimeError(
                        "output directory changed during staging setup"
                    ) from error
                except OSError as error:
                    raise RuntimeError(
                        "cannot snapshot the active output directory"
                    ) from error
            collisions: list[str] = []
            try:
                for _ in range(100):
                    candidate = f".astia-stage-{os.urandom(8).hex()}"
                    try:
                        os.mkdir(candidate, 0o700, dir_fd=parent)
                    except FileExistsError:
                        collisions.append(candidate)
                        continue
                    staging_name = candidate
                    break
                else:
                    raise OSError("cannot allocate a staging directory")
            except BaseException as error:
                if collisions:
                    preserved = ", ".join(
                        lexical_path(path.parent, name)
                        for name in collisions
                    )
                    cls._report_deferred(
                        f"staging allocation failed; preserved pre-existing "
                        f"paths: {preserved}: {error}"
                    )
                raise
            try:
                staging = os.open(
                    staging_name,
                    os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW,
                    dir_fd=parent,
                )
                opened_info = os.fstat(staging)
                os.fchmod(staging, 0o700)
                opened_info = os.fstat(staging)
                if (
                    not stat.S_ISDIR(opened_info.st_mode)
                    or opened_info.st_uid != os.geteuid()
                    or stat.S_IMODE(opened_info.st_mode) != 0o700
                ):
                    raise RuntimeError(
                        "staging directory is not private and owned"
                    )
                named_info = os.stat(
                    staging_name, dir_fd=parent, follow_symlinks=False
                )
                if (
                    not stat.S_ISDIR(named_info.st_mode)
                    or not same_inode(named_info, opened_info)
                ):
                    raise RuntimeError(
                        "staging directory identity changed during creation"
                    )
                staging_info = opened_info
                staging_created = True
            except BaseException:
                if staging >= 0:
                    os.close(staging)
                    staging = -1
                raise
            if active_info is not None:
                try:
                    os.fchown(
                        staging,
                        active_info.st_uid,
                        active_info.st_gid,
                    )
                except OSError as error:
                    raise ValueError(
                        "cannot preserve output directory ownership"
                    ) from error
                os.fchmod(staging, stat.S_IMODE(active_info.st_mode))
            if active >= 0:
                replace_xattrs(active, staging, None, ".")
                current = OutputDirectory(path, active, owns_descriptor=False)
                owned = owned_reader(current)
                copy_unowned_entries(active, staging, owned, manifest_name)
            return cls(
                path,
                parent,
                path.name,
                active,
                active_info,
                active_snapshot,
                staging_name,
                staging_info,
                staging,
            )
        except BaseException:
            if staging_created and staging_info is not None:
                if staging >= 0:
                    removed, retained, cleanup_error = quarantine_and_remove(
                        parent,
                        staging_name,
                        staging_info,
                        staging,
                        ".astia-clean-",
                    )
                    if cleanup_error is not None:
                        cls._report_deferred(
                            f"staging cleanup failed; retained at "
                            f"{lexical_path(path.parent, retained)}: "
                            f"{cleanup_error}"
                        )
                else:
                    cls._report_deferred(
                        f"staging cleanup deferred; preserved at "
                        f"{lexical_path(path.parent, staging_name)}: "
                        f"directory handle unavailable"
                    )
            if staging >= 0:
                os.close(staging)
            if active >= 0:
                os.close(active)
            os.close(parent)
            raise

    def __enter__(self) -> "TransactionalDirectory":
        return self

    def __exit__(self, *_: object) -> None:
        if not self._committed:
            self._remove_staging_if_owned()
        if self._staging >= 0:
            os.close(self._staging)
            self._staging = -1
        if self._active >= 0:
            os.close(self._active)
            self._active = -1
        os.close(self._parent)

    def _remove_staging_if_owned(self) -> None:
        if self._staging < 0 or self._staging_info is None:
            self._report_deferred(
                f"staging cleanup deferred; preserved at "
                f"{self._full_path(self._staging_name)}: "
                f"directory handle unavailable"
            )
            return
        removed, retained, error = quarantine_and_remove(
            self._parent,
            self._staging_name,
            self._staging_info,
            self._staging,
            ".astia-clean-",
        )
        if error is not None:
            self._report_deferred(
                f"staging cleanup failed; retained at "
                f"{self._full_path(retained)}: {error}"
            )

    def _active_matches(self) -> bool:
        try:
            info = os.stat(
                self._active_name,
                dir_fd=self._parent,
                follow_symlinks=False,
            )
        except FileNotFoundError:
            return self._active_info is None
        return self._active_info is not None and same_inode(info, self._active_info)

    def _close_active(self) -> None:
        if self._active >= 0:
            os.close(self._active)
            self._active = -1

    def _close_staging(self) -> None:
        if self._staging >= 0:
            os.close(self._staging)
            self._staging = -1

    @staticmethod
    def _report_deferred(message: str) -> None:
        try:
            warnings.warn(message, RuntimeWarning)
            return
        except Exception:
            pass
        try:
            print(f"warning: {message}", file=sys.stderr)
        except Exception:
            pass

    def _full_path(self, name: str) -> str:
        return lexical_path(self.path.parent, name)

    def _preserve_retired(self) -> tuple[str | None, Exception | None]:
        try:
            retired_name = f".astia-retired-{os.urandom(8).hex()}"
        except Exception as error:
            return None, error
        try:
            rename_noreplace(self._parent, self._staging_name, retired_name)
            return retired_name, None
        except Exception as error:
            return None, error

    def _assert_active_unchanged(self) -> None:
        if not self._active_matches():
            raise RuntimeError("output directory changed during publication")
        if self._active < 0:
            return
        try:
            current_snapshot = directory_snapshot(self._active)
        except OSError as error:
            raise RuntimeError(
                "output directory content changed during publication"
            ) from error
        if current_snapshot != self._active_snapshot:
            raise RuntimeError(
                "output directory content changed during publication"
            )

    def commit(self, before_activation=None) -> None:
        self.output.sync()
        self._assert_active_unchanged()
        if before_activation is not None:
            before_activation()
        self._assert_active_unchanged()
        if self._active_info is None:
            rename_noreplace(self._parent, self._staging_name, self._active_name)
            self._committed = True
        else:
            exchange_directories(
                self._parent,
                self._staging_name,
                self._active_name,
            )
            self._committed = True
            retired_unchanged = False
            retired_error = None
            try:
                retired_snapshot = directory_snapshot(self._active)
                retired_unchanged = retirement_snapshots_match(
                    retired_snapshot, self._active_snapshot
                )
            except Exception as error:
                retired_error = error
            if retired_unchanged:
                removed, retained, cleanup_error = quarantine_and_remove(
                    self._parent,
                    self._staging_name,
                    self._active_info,
                    self._active,
                    ".astia-clean-",
                )
                if cleanup_error is not None:
                    self._report_deferred(
                        f"deferred cleanup: retired generation at "
                        f"{self._full_path(retained)}: {cleanup_error}"
                    )
            else:
                retained, preserve_error = self._preserve_retired()
                retained_name = (
                    retained if retained is not None else self._staging_name
                )
                retained_path = self._full_path(retained_name)
                causes = []
                if retired_error is not None:
                    causes.append(str(retired_error))
                if preserve_error is not None:
                    causes.append(str(preserve_error))
                message = (
                    f"deferred cleanup: retired generation retained at "
                    f"{retained_path}"
                )
                if causes:
                    message += ": " + "; ".join(causes)
                self._report_deferred(message)
            try:
                self._close_active()
            except Exception:
                pass
        try:
            self._close_staging()
        except Exception:
            pass
        try:
            os.fsync(self._parent)
        except Exception:
            pass


def stale_generated_assets(manifest_text: str | None) -> set[str]:
    if manifest_text is None:
        return set()
    try:
        previous = json.loads(manifest_text)
    except (json.JSONDecodeError, UnicodeDecodeError):
        return set()
    pages = previous.get("pages", []) if isinstance(previous, dict) else []
    stale: set[str] = set()
    if isinstance(pages, list):
        for page in pages:
            if not isinstance(page, dict):
                continue
            name = page.get("markdown")
            if isinstance(name, str) and generated_syscall_asset(name):
                stale.add(name)
    return stale


def export_section2(
    export_dir: Path,
    canonical: list[tuple[int, str]],
    pages: dict[tuple[str, int], tuple[str, str]],
    sources: dict[tuple[str, int], Path],
    source_root: Path,
    abi_header: Path,
    raw_synopses: set[str] | None = None,
    trusted_root: Path | None = None,
    metadata: dict[str, dict] | None = None,
    authored: bool = False,
) -> None:
    raw_synopses = raw_synopses or set()
    metadata = metadata or {}
    repo_root = ROOT.parent.parent
    root = trusted_root if trusted_root is not None else repo_root
    with TransactionalDirectory.begin(
        export_dir,
        root,
        "manifest.json",
        lambda output: stale_generated_assets(output.read_manifest("manifest.json")),
    ) as transaction:
        output = transaction.output
        records = []
        for syscall_id, name in canonical:
            body, _ = pages[(name, 2)]
            source = sources.get((name, 2), abi_header)
            declared = metadata.get(name)
            slug = declared["slug"] if declared else f"syscall-{syscall_id}-{name.replace('_', '-')}"
            markdown_name = f"{slug}.md"
            output.write_atomic(
                markdown_name,
                body
                if authored and declared
                else render_syscall_markdown(name, body, 2, name in raw_synopses),
            )
            try:
                source_path = source.resolve().relative_to(repo_root).as_posix()
            except ValueError:
                source_path = source.as_posix()
            group = group_for_source(source, source_root)
            subsystem, category = taxonomy_for_group(group)
            record = {
                "id": syscall_id,
                "name": name,
                "slug": slug,
                "title": f"{name}(2)",
                "subsystem": declared["subsystem"] if declared else subsystem,
                "category": declared["category"] if declared else category,
                "source_path": declared["sourcePath"]
                if declared
                else authored_source_path(name, subsystem, category),
                "source": declared["source"] if declared else source_path,
                "markdown": markdown_name,
                "synopsis_kind": declared["synopsisKind"]
                if declared
                else "raw"
                if name in raw_synopses
                else "public",
            }
            if declared:
                record["synopsis"] = declared["synopsis"]
            records.append(record)
        manifest = {"section": 2, "count": len(records), "pages": records}
        output.write_atomic("manifest.json", json.dumps(manifest, indent=2) + "\n")
        transaction.commit()


def render(name: str, body: str, section: int, stylesheet: str) -> str:
    sections = []
    for heading, lines in body_sections(body).items():
        content = "\n".join(lines).strip()
        if not content:
            continue
        escaped = html.escape(content)
        if heading == "ERRORS":
            entries = error_entries(content)
            if entries is None:
                html_body = "<p>" + html.escape(sentence(content)) + "</p>"
            else:
                rendered_items = []
                for code, reason in entries:
                    rendered_items.append(
                        f"<li><code>{html.escape(code)}</code>"
                        f"<ul class=\"man-list man-list--detail\"><li>"
                        f"{html.escape(reason)}</li></ul></li>"
                    )
                html_body = "<ul class=\"man-list\">" + "".join(rendered_items) + "</ul>"
        elif heading == "RETURN VALUE":
            items = [
                item.strip()
                for item in re.split(r";\s*|\n\s*(?=-E[A-Z])|,\s*(?=-E[A-Z])", content)
                if item.strip()
            ]
            rendered_items = "".join(f"<li>{html.escape(item)}</li>" for item in items)
            html_body = "<ul class=\"man-list\">" + rendered_items + "</ul>"
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
<link rel=\"stylesheet\" href=\"{html.escape(stylesheet, quote=True)}\"></head>
<body><main class=\"surface surface__flex\">
<article class=\"surface__level__1 surface__minor\">
<h1>{html.escape(title)}</h1>
<nav><a href=\"index.html\">Astia manual index</a></nav>
{''.join(sections)}
</article></main></body></html>\n"""


MAN7_MANIFEST = ".astia-man7-manifest.json"


def read_owned_files(output: OutputDirectory) -> set[str]:
    text = output.read_manifest(MAN7_MANIFEST)
    if text is None:
        return set()
    try:
        previous = json.loads(text)
    except (json.JSONDecodeError, UnicodeDecodeError):
        return set()
    files = previous.get("files") if isinstance(previous, dict) else None
    if not isinstance(files, list):
        return set()
    owned: set[str] = set()
    for name in files:
        if not isinstance(name, str):
            continue
        try:
            validated_relative_path(name)
        except ValueError:
            continue
        owned.add(name)
    return owned


def write_man_manifest(output: OutputDirectory, files: set[str]) -> None:
    output.write_atomic(
        MAN7_MANIFEST,
        json.dumps({"files": sorted(files)}, indent=2) + "\n",
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path, help="syscall source directory")
    parser.add_argument("output", type=Path, help="generated site directory")
    parser.add_argument("--css", type=Path, default=DEFAULT_CSS)
    parser.add_argument("--libc-source", type=Path, default=None,
                        help="libc source root (defaults to the repository libc directory)")
    parser.add_argument("--docs", type=Path, default=None,
                        help="architecture Markdown directory (defaults to repository docs)")
    parser.add_argument("--abi-header", type=Path, default=None,
                        help="public section-2 syscall ABI header")
    parser.add_argument("--export-section2", type=Path, default=None,
                        help="write section-2 Markdown pages and manifest.json here")
    args = parser.parse_args()

    repo_root = ROOT.parent.parent
    docs_root = args.docs if args.docs is not None else repo_root / "docs"
    try:
        authored = load_authored_docs(repo_root, docs_root)
    except ValueError as error:
        raise SystemExit(str(error)) from error

    pages: dict[tuple[str, int], tuple[str, str]] = {}
    sources: dict[tuple[str, int], Path] = {}
    metadata: dict[str, dict] = {}
    for record in authored.section2:
        group = taxonomy_group(record["subsystem"], record["category"])
        pages[(record["name"], 2)] = (record["markdown"], group)
        sources[(record["name"], 2)] = repo_root / record["source"]
        metadata[record["name"]] = record

    libc_root = args.libc_source
    if libc_root is None:
        candidate = args.source.resolve().parents[1] / "libc"
        libc_root = candidate if candidate.exists() else None
    if libc_root is not None and (libc_root / "include").exists():
        for name, body, group, section in libc_pages():
            pages.setdefault((name, section), (body, group))

    abi_header = args.abi_header or args.source.parent / "include/astia/api/syscalls.h"
    if not abi_header.exists():
        raise SystemExit(f"missing section-2 ABI header: {abi_header}")
    canonical = [(record["id"], record["name"]) for record in authored.section2]
    raw_synopses = {
        record["name"] for record in authored.section2 if record["synopsisKind"] == "raw"
    }

    overviews = overview_pages(authored.section7)
    expected_pages = {f"{name}.{section}.html" for name, section in pages}
    expected_overviews = {f"{name}.7.html" for name, _, _ in overviews}
    current_owned = {"nodivia.css", "index.html"} | expected_pages | expected_overviews
    groups_by_section: dict[int, dict[str, dict[str, list[tuple[str, int]]]]] = {}
    for (name, section), (_, group) in pages.items():
        parts = group.split("/", 1)
        root, child = parts[0], parts[1] if len(parts) > 1 else ""
        groups_by_section.setdefault(section, {}).setdefault(root, {}).setdefault(child, []).append((name, section))

    grouped_parts = []
    section_labels = {
        2: "MAN 2 - SYSTEM CALLS",
        3: "MAN 3 - LIBC APIs",
        7: "MAN 7 - OVERVIEWS",
    }
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
    if overviews:
        grouped_parts.append(
            "<section class='surface__level__0 man-section'><h2>MAN 7 - OVERVIEWS</h2><ul>"
            + "".join(
                f"<li><a href='{html.escape(name)}.7.html'>{html.escape(name)}(7)</a></li>"
                for name, _, _ in overviews
            )
            + "</ul></section>"
        )
    grouped = "".join(grouped_parts)
    with TransactionalDirectory.begin(
        args.output,
        repo_root,
        MAN7_MANIFEST,
        read_owned_files,
    ) as transaction:
        output = transaction.output
        output.write_atomic("nodivia.css", args.css.read_text())
        for (name, section), (body, _) in sorted(pages.items()):
            page_name = f"{name}.{section}.html"
            stylesheet = stylesheet_href(page_name)
            output.write_atomic(
                page_name,
                render_markdown_page(name, body, section, stylesheet)
                if section == 2
                else render(name, body, section, stylesheet),
            )
        for name, title, body in overviews:
            page_name = f"{name}.7.html"
            output.write_atomic(
                page_name,
                render_overview(name, title, body, stylesheet_href(page_name)),
            )
        output.write_atomic(
            "index.html",
            "<!doctype html><html><head><meta charset='utf-8'><title>Astia manual</title>"
            f"<link rel='stylesheet' href='{html.escape(stylesheet_href('index.html'), quote=True)}'>"
            "</head><body><main class='surface'>"
            "<h1>Astia manual</h1>" + grouped + "</main></body></html>\n",
        )
        write_man_manifest(output, current_owned)
        transaction.commit()
    if args.export_section2 is not None:
        export_section2(
            args.export_section2,
            canonical,
            pages,
            sources,
            args.source,
            abi_header,
            raw_synopses,
            metadata=metadata,
            authored=True,
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
