#pragma once

#include <astia/proc/process.hpp>

namespace astia::proc {

    /// Create the initial kernel process and bootstrap thread.  This
    /// establishes ownership/current-object semantics only; scheduling
    /// remains disabled until architecture context switching exists.
    auto init() -> bool;

    auto kernel_process() -> process*;
    auto current_process() -> process*;
    auto current_thread() -> thread*;
    auto allocate_tid() -> tid_t;
    /// Next process id (slot 0 / the kernel process is pid 0; userspace
    /// starts at 1).  spawnve and fork both draw from here.
    auto allocate_pid() -> pid_t;
    /// Insert a process into the registry.  Fails when it is full.
    auto register_process(const mm::shptr<process>& value) -> bool;
    auto set_current_thread(thread* value) -> void;

    /// Spawn an executable by VFS path.  argv/envp are materialized on
    /// the child's user stack as the standard startup frame
    /// (argc/argv/envp + AT_NULL), built in an arch-neutral way; the
    /// architecture only consumes the resulting entry stack pointer.
    /// On failure an empty shptr comes back and *error (when given)
    /// receives the negated errno for the syscall boundary to return.
    auto spawnve(const char* path, char* const argv[], char* const envp[],
                 int* error = nullptr) -> mm::shptr<process>;

    /// Look up a live process by pid in the registry.
    auto find_process(pid_t id) -> process*;

    /// Wait for a child of the current process to exit and reap it.
    /// pid > 0 waits for that child, pid == -1 for any child.  Returns
    /// the reaped pid with *status set, or -ECHILD/-EINVAL/-ESRCH.
    auto waitpid(i32 pid, i32* status) -> i64;

    /// Duplicate the calling process copy-on-write: the child shares
    /// every mapping (each backing region gains one reference, private
    /// writable pages lose their write bits on both sides) and resumes
    /// the parent's ring-3 register image with rax = 0 — the child's
    /// fork() return.  `uctx` is the syscall-entry register image the
    /// arch dispatch packed.  Returns the child pid, or -ESRCH (no
    /// user process) / -EAGAIN (resource or registry failure).
    auto fork(const arch::user_context* uctx) -> i64;

} // namespace astia::proc
