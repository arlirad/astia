#include "../ipc/futex.hpp"
#include "../ipc/mapping.hpp"
#include "mapping.hpp"
#include "proc.hpp"
#include "scheduler.hpp"
#include "syscall.hpp"
#include "syscalls/getpgrp.hpp"
#include "syscalls/getpid.hpp"
#include "syscalls/gettid.hpp"
#include "syscalls/ipc/futex.hpp"
#include "syscalls/kill.hpp"
#include "syscalls/memory/mmap.hpp"
#include "syscalls/memory/munmap.hpp"
#include "syscalls/process/chdir.hpp"
#include "syscalls/process/execve.hpp"
#include "syscalls/process/exit.hpp"
#include "syscalls/process/fork.hpp"
#include "syscalls/process/getcwd.hpp"
#include "syscalls/process/rlimit.hpp"
#include "syscalls/process/spawnve.hpp"
#include "syscalls/process/waitpid.hpp"
#include "syscalls/setpgid.hpp"
#include "syscalls/setsid.hpp"
#include "syscalls/signal/sigaction.hpp"
#include "syscalls/signal/sigprocmask.hpp"
#include "syscalls/signal/sigreturn.hpp"
#include "syscalls/signal/sigsuspend.hpp"
#include "syscalls/thread/thread_create.hpp"
#include "syscalls/thread/thread_create2.hpp"
#include "syscalls/thread/thread_detach.hpp"
#include "syscalls/thread/thread_exit.hpp"
#include "syscalls/thread/thread_join.hpp"
#include "syscalls/thread/yield.hpp"
#include "user_mem.hpp"
#include <astia/api/errno.h>
#include <astia/api/signal.h>
#include <astia/api/syscalls.h>
#include <astia/arch/context.hpp>
#include <astia/arch/cpu.hpp>
#include <astia/arch/interrupts.hpp>
#include <astia/log.hpp>
#include <astia/mm/heap.hpp>

namespace astia::proc {
    namespace syscall {
        i64 getsid(i64 raw_target) {
            auto target = mapper<proc::getsid_target>::from(raw_target);
            return target ? proc::getsid(target.value.pid) : -ESRCH;
        }
        i64 process_snapshot(i64 output, i64 capacity) {
            if (output < 0 || capacity < 0 || capacity > ASTIA_PROCESS_SNAPSHOT_MAX)
                return -EINVAL;
            return proc::process_snapshot(static_cast<u64>(output),
                                          static_cast<u32>(capacity));
        }
        i64 getuid();
        i64 geteuid();
        i64 getgid();
        i64 getegid();
        i64 setuid(i64);
        i64 seteuid(i64);
        i64 setgid(i64);
        i64 setegid(i64);
        i64 setreuid(i64, i64);
        i64 setregid(i64, i64);
        i64 getgroups(i64, i64);
        i64 setgroups(i64, i64);
    }

    i64 futex_wait(u64 address, u32 expected);
    i64 futex_wake(u64 address, u32 count);

    namespace {
        /// This module's log source: the same one proc.cpp logs under.
        using logs = astia::log::context<"proc">;

        void deliver_default_signal(arch::syscall_frame& raw_frame) {
            auto* owner = current_process();
            auto* self  = current_thread();
            if (owner == nullptr || self == nullptr) {
                return;
            }
            const signal_number signal = owner->signals().take_unblocked();
            if (!valid_signal(signal)) {
                return;
            }
            const signal_disposition disposition = owner->signals().disposition(signal);
            const bool               suspended   = self->has_signal_restore_mask();
            const u32                restore_mask =
                suspended ? self->signal_restore_mask() : owner->signals().blocked();
            if (disposition.action == signal_action::handler) {
                auto&           frame = raw_frame;
                constexpr usize bytes = sizeof(astia_x86_64_signal_context_t) + 8;
                if (frame.rsp < bytes || disposition.restorer == 0)
                    goto fatal_signal;
                const u64 context = frame.rsp - sizeof(astia_x86_64_signal_context_t);
                const u64 retaddr = context - 8;
                if (!sys::user_range_ok(sys::user_range::bytes(retaddr, bytes)) ||
                    !sys::user_range_ok(sys::user_range::bytes(
                        reinterpret_cast<u64>(disposition.handler), 1)))
                    goto fatal_signal;
                astia_x86_64_signal_context_t saved{
                    frame.nr,  frame.rbx, frame.rip,    frame.a2,     frame.a1,
                    frame.a0,  frame.a4,  frame.a5,     frame.a3,     frame.rflags,
                    frame.r12, frame.r13, frame.r14,    frame.r15,    frame.rbp,
                    frame.rip, frame.rsp, frame.rflags, restore_mask, 0};
                if (!sys::copy_out(sys::user_range::bytes(context, sizeof saved),
                                   &saved) ||
                    !sys::copy_out(sys::user_range::bytes(retaddr, 8),
                                   &disposition.restorer))
                    goto fatal_signal;
                owner->signals().block(signal_bit(signal) | disposition.mask);
                frame.nr = ~u64{0};
                frame.a0 = static_cast<u64>(signal);
                frame.a1 = frame.a2 = frame.a3 = frame.a4 = frame.a5 = 0;
                frame.rip = reinterpret_cast<u64>(disposition.handler);
                frame.rsp = retaddr;
                frame.rbx = frame.rbp = frame.r12 = frame.r13 = frame.r14 = frame.r15 = 0;
                return;
            }
            if (default_action(signal) == signal_default::terminate ||
                default_action(signal) == signal_default::core) {
                owner->signal_termination(signal);
                self->exit(reinterpret_cast<void*>(128u + static_cast<u8>(signal)));
                __builtin_unreachable();
            }
            if (default_action(signal) == signal_default::stop ||
                default_action(signal) == signal_default::continue_) {
                // Stop/continue transitions are applied synchronously by
                // kill/terminal group delivery.  Consuming the pending bit
                // here must not requeue SIGTSTP forever (or turn SIGCONT
                // into an ordinary ignored signal).
                if (suspended) {
                    owner->signals().set_blocked(restore_mask);
                    self->clear_signal_restore_mask();
                }
                return;
            }
            // A default-ignored signal is consumed here.  Re-queueing it
            // would make every syscall exit deliver SIGCHLD forever when a
            // shell has no explicit SIGCHLD disposition.
            if (default_action(signal) == signal_default::ignore) {
                if (suspended) {
                    owner->signals().set_blocked(restore_mask);
                    self->clear_signal_restore_mask();
                }
                return;
            }
            owner->signals().send(signal);
            return;
        fatal_signal:
            owner->signal_termination(signal);
            self->exit(reinterpret_cast<void*>(128u + static_cast<u8>(signal)));
            __builtin_unreachable();
        }

        /* spawnve's three arguments cross as user pointers: a path
         * string and two NULL-terminated pointer vectors, each element
         * of a vector another string.  Everything lands in one kernel
         * block before proc::spawnve sees it — spawnve never blocks, so
         * unlike waitpid there is nothing to copy out and no pointer to
         * re-check.  Bounds: 256 B per string, 32 elements per vector;
         * past that the vectors are rejected with -E2BIG rather than
         * walked forever. */

        /// mmap — private memory in the caller's address space:
        /// anonymous, or a snapshot of an open file.  `addr` is a
        /// placement hint, never a forced address.  Never blocks.

        /// munmap — remove [addr, addr + len) from the caller's
        /// address space.  Never blocks.

        /// fork — duplicate the caller copy-on-write.  The one handler
        /// that takes the trap image itself instead of its six argument
        /// registers: the child resumes at the caller's own ring-3
        /// instruction, so proc::fork wants the caller's whole register
        /// state.  Never blocks.

        /// The #PF path's repair offer for ring-3 faults: the process
        /// resolves its copy-on-write pages (fork's shared mappings);
        /// everything it declines stays fatal.
        bool user_fault(u64 addr, bool write) {
            auto* owner = current_process();
            return owner != nullptr && owner->cow_fault(addr, write);
        }
    }

    bool register_syscalls() {
        return sys::register_syscall<syscall::getpid>(sys::syscall_id::getpid) &&
               sys::register_syscall<syscall::gettid>(sys::syscall_id::gettid) &&
               sys::register_syscall<syscall::kill>(sys::syscall_id::kill) &&
               sys::register_syscall<syscall::sigaction>(sys::syscall_id::sigaction) &&
               sys::register_syscall<syscall::sigreturn>(sys::syscall_id::sigreturn) &&
               sys::register_syscall<syscall::sigprocmask>(
                   sys::syscall_id::sigprocmask) &&
               sys::register_syscall<syscall::yield>(sys::syscall_id::yield) &&
               sys::register_syscall<syscall::exit>(sys::syscall_id::exit) &&
               sys::register_syscall<syscall::waitpid>(sys::syscall_id::waitpid) &&
               sys::register_syscall<syscall::chdir>(sys::syscall_id::chdir) &&
               sys::register_syscall<syscall::getcwd>(sys::syscall_id::getcwd) &&
               sys::register_syscall<syscall::getuid>(sys::syscall_id::getuid) &&
               sys::register_syscall<syscall::geteuid>(sys::syscall_id::geteuid) &&
               sys::register_syscall<syscall::getgid>(sys::syscall_id::getgid) &&
               sys::register_syscall<syscall::getegid>(sys::syscall_id::getegid) &&
               sys::register_syscall<syscall::setuid>(sys::syscall_id::setuid) &&
               sys::register_syscall<syscall::seteuid>(sys::syscall_id::seteuid) &&
               sys::register_syscall<syscall::setgid>(sys::syscall_id::setgid) &&
               sys::register_syscall<syscall::setegid>(sys::syscall_id::setegid) &&
               sys::register_syscall<syscall::setreuid>(sys::syscall_id::setreuid) &&
               sys::register_syscall<syscall::setregid>(sys::syscall_id::setregid) &&
               sys::register_syscall<syscall::getgroups>(sys::syscall_id::getgroups) &&
               sys::register_syscall<syscall::setgroups>(sys::syscall_id::setgroups) &&
               sys::register_syscall<syscall::process_snapshot>(
                   sys::syscall_id::process_snapshot) &&
               sys::register_syscall<syscall::spawnve>(sys::syscall_id::spawnve) &&
               sys::register_syscall<syscall::execve>(sys::syscall_id::execve) &&
               sys::register_syscall<syscall::sigsuspend>(sys::syscall_id::sigsuspend) &&
               sys::register_syscall<syscall::getpgrp>(sys::syscall_id::getpgrp) &&
               sys::register_syscall<syscall::setpgid>(sys::syscall_id::setpgid) &&
               sys::register_syscall<syscall::setsid>(sys::syscall_id::setsid) &&
               sys::register_syscall<syscall::getsid>(sys::syscall_id::getsid) &&
               sys::register_syscall<syscall::getrlimit>(sys::syscall_id::getrlimit) &&
               sys::register_syscall<syscall::setrlimit>(sys::syscall_id::setrlimit) &&
               sys::register_syscall<syscall::mmap>(sys::syscall_id::mmap) &&
               sys::register_syscall<syscall::munmap>(sys::syscall_id::munmap) &&
               sys::register_syscall<syscall::fork>(sys::syscall_id::fork) &&
               sys::register_syscall<syscall::futex>(sys::syscall_id::futex) &&
               sys::register_syscall<syscall::thread_create>(
                   sys::syscall_id::thread_create) &&
               sys::register_syscall<syscall::thread_exit>(
                   sys::syscall_id::thread_exit) &&
               sys::register_syscall<syscall::thread_join>(
                   sys::syscall_id::thread_join) &&
               sys::register_syscall<syscall::thread_detach>(
                   sys::syscall_id::thread_detach) &&
               sys::register_syscall<syscall::thread_create2>(
                   sys::syscall_id::thread_create2) &&
               sys::register_exit_hook(deliver_default_signal) &&
               arch::register_user_fault_handler(user_fault);
    }

} // namespace astia::proc
