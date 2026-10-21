#pragma once

#include "process.hpp"

namespace astia::proc {

    /// Create the initial kernel process and bootstrap thread.  This
    /// establishes ownership/current-object semantics only; scheduling
    /// remains disabled until architecture context switching exists.
    bool init();

    process* kernel_process();
    process* current_process();
    thread*  current_thread();
    tid_t    allocate_tid();
    /// Next process id (slot 0 / the kernel process is pid 0; userspace
    /// starts at 1).  spawnve and fork both draw from here.
    pid_t allocate_pid();
    /// Insert a process into the registry.  Fails when it is full.
    bool register_process(const mm::shptr<process>& value);
    void set_current_thread(thread* value);

    /// Spawn an executable by VFS path.  argv/envp are materialized on
    /// the child's user stack as the standard startup frame
    /// (argc/argv/envp + AT_NULL), built in an arch-neutral way; the
    /// architecture only consumes the resulting entry stack pointer.
    /// On failure an empty shptr comes back and *error (when given)
    /// receives the negated errno for the syscall boundary to return.
    mm::shptr<process> spawnve(const char* path, char* const argv[], char* const envp[],
                               int* error = nullptr);

    /// Look up a live process by pid in the registry.
    process* find_process(pid_t id);

    /// Process-group/session operations for the calling process.
    pid_t getpgrp();
    i64   setpgid(i32 pid, i32 pgid);
    i64   setsid();
    /// Send a signal to every live member of a process group.  Signal zero
    /// only checks that the group has a live member.
    i64 signal_process_group(pid_t group, i32 signal);

    /// Wait for a child of the current process to exit and reap it.
    /// pid > 0 waits for that child, pid == -1 for any child.  Returns
    /// the reaped pid with *status set, or -ECHILD/-EINVAL/-ESRCH.
    i64 waitpid(i32 pid, i32* status, u32 options = 0);

    /// Duplicate the calling process copy-on-write: the child shares
    /// every mapping (each backing region gains one reference, private
    /// writable pages lose their write bits on both sides) and resumes
    /// the caller's ring-3 image with rax = 0 — the child's fork()
    /// return.  `uctx` is that image (arch::caller_image); the
    /// thread pointer is the address space's, and this takes it from the
    /// parent.  Returns the child pid, or -ESRCH (no user process) /
    /// -EAGAIN (resource or registry failure).
    i64 fork(const arch::user_context& uctx);

} // namespace astia::proc
