#pragma once

#include <astia/optional.hpp>

#include "process.hpp"

namespace astia::proc {
    /// getsid's POSIX selector: zero means the caller, otherwise a positive
    /// process ID.  This is deliberately distinct from the generic pid
    /// mapper, whose zero value is invalid for other process syscalls.
    struct getsid_target {
        pid_t pid;
    };

    enum class wait_options : u32 {
        none      = 0,
        nohang    = 1u << 0,
        untraced  = 1u << 1,
        continued = 1u << 3,
    };

    inline constexpr u32 valid_wait_option_bits =
        static_cast<u32>(wait_options::nohang) |
        static_cast<u32>(wait_options::untraced) |
        static_cast<u32>(wait_options::continued);

    constexpr u32 raw_wait_options(wait_options options) {
        return static_cast<u32>(options);
    }

    constexpr bool has_wait_option(wait_options options, wait_options option) {
        return (raw_wait_options(options) & raw_wait_options(option)) != 0;
    }

    constexpr pgrp_t wait_group_from_selector(i32 pid) {
        return pgrp_from_raw(static_cast<u32>(-static_cast<i64>(pid)));
    }

    constexpr pid_t wait_pid_from_selector(i32 pid) {
        return pid_from_raw(static_cast<u32>(pid));
    }

    constexpr wait_options operator|(wait_options a, wait_options b) {
        return static_cast<wait_options>(static_cast<u32>(a) | static_cast<u32>(b));
    }

    /// Create the initial kernel process and bootstrap thread.  This
    /// establishes ownership/current-object semantics only; scheduling
    /// remains disabled until architecture context switching exists.
    bool init();

    process* kernel_process();
    process* current_process();
    thread*  current_thread();
    void     terminate_current_if_killed();
    tid_t    allocate_tid();
    /// Next process id (slot 0 / the kernel process is pid 0; userspace
    /// starts at 1).  spawnve and fork both draw from here.
    pid_t allocate_pid();
    /// Insert a process into the registry.  Fails when it is full.
    bool register_process(const mm::shptr<process>& value);
    /// Remove a process that failed before its first thread could be
    /// published.  The caller must retain another reference to `value`.
    bool unregister_process(process* value);
    i64  process_snapshot(u64 output, u32 capacity);
    void set_current_thread(thread* value);
    void expire_signal_waiters();
    void expire_alarms();

    /// Spawn an executable from an already-open VFS file. ELF loading is
    /// streamed through the asynchronous filesystem and argv/envp are
    /// materialized on the child's user stack after loading completes.
    core::task<mm::shptr<process>> spawnve(process* parent, mm::shptr<fs::open_file> file,
                                           usize length, char* const argv[],
                                           char* const envp[]);

    /// Look up a live process by pid in the registry.
    mm::shptr<process> find_process(pid_t id);
    /// Look up a process while retaining an owning reference for use after
    /// the registry lock is released.
    mm::shptr<process> retain_process(pid_t id);

    /// Process-group/session operations for the calling process.
    pgrp_t getpgrp();
    i64    getpgid(pid_t target);
    i64    getsid(pid_t target);
    /// Attach the supplied tty to the current session leader.
    i64 acquire_controlling_terminal(const mm::shptr<fs::open_file>& terminal);
    /// True when a live process in `group` belongs to `session`.
    bool process_group_exists(pgrp_t group, pid_t session);

    /// Terminal line-discipline hooks.  These deliberately operate on the
    /// caller's native process group rather than manufacturing IDs in tty.
    enum class terminal_access_result : u8 {
        allowed,
        retry,
        interrupted,
        denied,
        io_error,
    };

    i64 terminal_signal_foreground(pgrp_t foreground, signal_number signal);
    terminal_access_result   terminal_read_allowed(pgrp_t foreground);
    terminal_access_result   terminal_write_allowed(pgrp_t foreground, bool tostop);
    terminal_access_result   terminal_set_foreground_allowed(pgrp_t foreground,
                                                             pgrp_t requested);
    mm::shptr<fs::open_file> controlling_terminal();
    bool                     has_controlling_terminal();
    i64                      setpgid(pid_t pid, pgrp_t pgid);
    i64                      setsid();

    /// Credentials of a user-space signal sender. `kill()` group sends pass
    /// them so only permitted members are signalled; kernel-internal sends
    /// leave the filter out.
    struct signal_sender {
        u32 uid;
        u32 euid;
    };

    /// Send a signal to every live member of a process group.  Signal zero
    /// only checks that the group has a permitted live member.  With a
    /// sender, members failing the credential check are skipped and a group
    /// with no permitted member reports -EPERM.
    i64 signal_process_group(pgrp_t group, signal_number signal, signal_info info = {},
                             const signal_sender* sender = nullptr);

    /// Send a signal to every live process the sender may signal, skipping
    /// `exclude` (the caller): the kill(-1) form.
    i64 signal_all_processes(signal_number signal, signal_info info,
                             const signal_sender& sender, pid_t exclude);

    /// Wait for a child of the current process to exit and reap it.
    /// pid > 0 waits for that child, pid == -1 for any child.  Returns
    /// the reaped pid with *status set, or -ECHILD/-EINVAL/-ESRCH.
    core::task<i64> waitpid(i32 pid, i32* status,
                            wait_options options = wait_options::none);

    /// Duplicate the calling process copy-on-write: the child shares
    /// every mapping (each backing region gains one reference, private
    /// writable pages lose their write bits on both sides) and resumes
    /// the caller's ring-3 image with rax = 0 — the child's fork()
    /// return.  `uctx` is that image (arch::caller_image); the
    /// thread pointer is the address space's, and this takes it from the
    /// parent.  Returns the child pid, or -ESRCH (no user process) /
    /// -EAGAIN (resource or registry failure).
    core::task<i64> fork(const arch::user_context& uctx);

    /// Boot entry boundary: start the single boot coroutine and park the
    /// seed thread until it completes.  The body runs early init inline
    /// before the scheduler exists; run_boot captures the seed process and
    /// thread after the first suspension, when the scheduler is up, and
    /// keeps every continuation on that thread.
    ///
    /// Do not replace this with core::det_task.  det_task owns the frame but
    /// not the current process/thread context: its continuations may be
    /// resumed from another CPU or completion context, while the boot body's
    /// spawnve/waitpid calls depend on the original seed thread's
    /// current_process()/current_thread().
    i64 run_boot(core::task<void> operation);
} // namespace astia::proc
