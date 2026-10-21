#include "proc.hpp"
#include "scheduler.hpp"
#include "syscall.hpp"
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

    i64 futex_wait(u64 address, u32 expected);
    i64 futex_wake(u64 address, u32 count);

    namespace {
        /// This module's log source: the same one proc.cpp logs under.
        using logs = astia::log::context<"proc">;

        i64 sys_getpid() {
            auto* process = current_process();
            return process == nullptr ? -ESRCH : static_cast<i64>(process->pid());
        }

        i64 sys_gettid() {
            auto* thread = current_thread();
            return thread == nullptr ? -ESRCH : static_cast<i64>(thread->tid());
        }

        i64 sys_kill(i32 pid, i32 value) {
            /* zsh probes a process group with kill(-pgrp, 0).  Signal zero
               is permission/existence checking only and must not enqueue a
               signal. */
            if (value < 0 || value >= static_cast<i32>(signal_count)) {
                return -EINVAL;
            }
            if (pid <= 0) {
                const pid_t group =
                    pid == 0 ? getpgrp() : static_cast<pid_t>(-static_cast<i64>(pid));
                return signal_process_group(group, value);
            }
            auto* target = find_process(static_cast<pid_t>(pid));
            if (target == nullptr) {
                return -ESRCH;
            }
            if (value == 0) {
                return 0;
            }
            if (!target->signals().send(static_cast<signal_number>(value))) {
                return -EINVAL;
            }
            // A signal is also a wakeup source for sigsuspend().  Child exit
            // has the same responsibility in process::thread_exited(); keep
            // direct kill delivery from leaving a suspended thread asleep.
            target->wake_signal_waiters();
            return 0;
        }

        i64 sys_getpgrp() {
            return static_cast<i64>(getpgrp());
        }
        i64 sys_setpgid(i32 pid, i32 pgid) {
            return setpgid(pid, pgid);
        }
        i64 sys_setsid() {
            return setsid();
        }

        i64 sys_sigaction(i32 value, u64 next_ptr, u64 old_ptr) {
            if (value <= 0 || value >= static_cast<i32>(signal_count)) {
                return -EINVAL;
            }
            const auto signal = static_cast<signal_number>(value);
            auto*      owner  = current_process();
            if (owner == nullptr) {
                return -ESRCH;
            }
            const auto old = owner->signals().disposition(signal);
            if (old_ptr != 0) {
                const auto range =
                    sys::user_range::bytes(old_ptr, sizeof(struct astia_sigaction));
                if (!sys::user_range_ok(range)) {
                    return -EFAULT;
                }
                struct astia_sigaction out{};
                out.handler  = old.action == signal_action::ignore ? ASTIA_SIG_IGN
                               : old.action == signal_action::handler
                                   ? reinterpret_cast<uintptr_t>(old.handler)
                                   : ASTIA_SIG_DFL;
                out.mask     = old.mask;
                out.flags    = 0;
                out.restorer = old.restorer;
                if (!sys::copy_out(range, &out)) {
                    return -EFAULT;
                }
            }
            if (next_ptr != 0) {
                const auto range =
                    sys::user_range::bytes(next_ptr, sizeof(struct astia_sigaction));
                if (!sys::user_range_ok(range)) {
                    return -EFAULT;
                }
                struct astia_sigaction next{};
                if (!sys::copy_in(&next, range)) {
                    return -EFAULT;
                }
                signal_disposition disposition{};
                if (next.handler == ASTIA_SIG_IGN) {
                    disposition.action = signal_action::ignore;
                }
                else if (next.handler != ASTIA_SIG_DFL) {
                    disposition.action   = signal_action::handler;
                    disposition.handler  = reinterpret_cast<signal_handler>(next.handler);
                    disposition.restorer = next.restorer;
                    disposition.mask     = next.mask;
                    if (disposition.restorer == 0 ||
                        !sys::user_range_ok(
                            sys::user_range::bytes(disposition.restorer, 1)))
                        return -EFAULT;
                }
                if (!owner->signals().disposition(signal, disposition)) {
                    return -EINVAL;
                }
            }
            return 0;
        }

        i64 sys_sigreturn(const arch::syscall_frame& input) {
            const auto range =
                sys::user_range::bytes(input.a0, sizeof(astia_x86_64_signal_context_t));
            if (!sys::user_range_ok(range))
                return -EFAULT;
            astia_x86_64_signal_context_t saved{};
            if (!sys::copy_in(&saved, range))
                return -EFAULT;
            if (saved.rip == 0 || saved.rsp == 0 || saved.rip >= arch::kernel_base ||
                saved.rsp >= arch::kernel_base || (saved.rflags & (1u << 9)) == 0)
                return -EFAULT;
            auto& frame  = const_cast<arch::syscall_frame&>(input);
            frame.nr     = ~u64{1};
            frame.a0     = saved.rdi;
            frame.a1     = saved.rsi;
            frame.a2     = saved.rdx;
            frame.a3     = saved.r10;
            frame.a4     = saved.r8;
            frame.a5     = saved.r9;
            frame.rip    = saved.rip;
            frame.rflags = saved.rflags;
            frame.rsp    = saved.rsp;
            frame.rbx    = saved.rbx;
            frame.rbp    = saved.rbp;
            frame.r12    = saved.r12;
            frame.r13    = saved.r13;
            frame.r14    = saved.r14;
            frame.r15    = saved.r15;
            if (auto* owner = current_process())
                owner->signals().set_blocked(saved.blocked);
            return static_cast<i64>(saved.rax);
        }

        i64 sys_sigprocmask(i32 how, u32 set, u64 old_ptr) {
            auto* owner = current_process();
            if (owner == nullptr) {
                return -ESRCH;
            }
            if (old_ptr != 0) {
                const auto range = sys::user_range::bytes(old_ptr, sizeof(u32));
                if (!sys::user_range_ok(range)) {
                    return -EFAULT;
                }
                const u32 old = owner->signals().blocked();
                if (!sys::copy_out(range, &old)) {
                    return -EFAULT;
                }
            }
            switch (how) {
            case ASTIA_SIG_BLOCK:
                owner->signals().block(set);
                break;
            case ASTIA_SIG_UNBLOCK:
                owner->signals().unblock(set);
                break;
            case ASTIA_SIG_SETMASK:
                owner->signals().set_blocked(set);
                break;
            default:
                return -EINVAL;
            }
            return 0;
        }

        i64 sys_sigsuspend(u64 mask_ptr) {
            auto* owner = current_process();
            auto* self  = current_thread();
            if (owner == nullptr || self == nullptr)
                return -ESRCH;
            const auto range = sys::user_range::bytes(mask_ptr, sizeof(u32));
            if (!sys::user_range_ok(range))
                return -EFAULT;
            u32 mask = 0;
            if (!sys::copy_in(&mask, range))
                return -EFAULT;
            // Match waitpid's register-before-scan protocol.  Interrupts stay
            // masked until either a pending signal is observed or this thread
            // is blocked, so delivery cannot fall between the check and the
            // waiter registration.
            arch::cpu::nointerrupts();
            owner->signals().set_blocked(mask);
            owner->add_signal_waiter(self);
            while (!owner->signals().has_unblocked_pending()) {
                // Signal delivery is performed by the syscall exit hook.
                // Signal senders clear the registration before waking us.
                self->state(thread_state::blocked);
                scheduler::yield();
            }
            owner->remove_signal_waiter(self);
            arch::cpu::interrupts();
            return -EINTR;
        }

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
                astia_x86_64_signal_context_t saved{frame.nr,
                                                    frame.rbx,
                                                    frame.rip,
                                                    frame.a2,
                                                    frame.a1,
                                                    frame.a0,
                                                    frame.a4,
                                                    frame.a5,
                                                    frame.a3,
                                                    frame.rflags,
                                                    frame.r12,
                                                    frame.r13,
                                                    frame.r14,
                                                    frame.r15,
                                                    frame.rbp,
                                                    frame.rip,
                                                    frame.rsp,
                                                    frame.rflags,
                                                    owner->signals().blocked(),
                                                    0};
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
                owner->signal_termination(static_cast<i32>(signal));
                self->exit(reinterpret_cast<void*>(128u + static_cast<u8>(signal)));
                __builtin_unreachable();
            }
            // A default-ignored signal is consumed here.  Re-queueing it
            // would make every syscall exit deliver SIGCHLD forever when a
            // shell has no explicit SIGCHLD disposition.
            if (default_action(signal) == signal_default::ignore)
                return;
            owner->signals().send(signal);
            return;
        fatal_signal:
            owner->signal_termination(static_cast<i32>(signal));
            self->exit(reinterpret_cast<void*>(128u + static_cast<u8>(signal)));
            __builtin_unreachable();
        }

        i64 sys_yield() {
            scheduler::yield();
            return 0;
        }

        i64 sys_exit(i64 status) {
            auto* thread = current_thread();
            if (thread == nullptr)
                return -ESRCH;
            logs::info("user thread exit pid=%d tid=%d status=%ld",
                       thread->owner() ? static_cast<int>(thread->owner()->pid()) : -1,
                       static_cast<int>(thread->tid()), status);
            thread->exit(reinterpret_cast<void*>(static_cast<usize>(status)));
            __builtin_unreachable();
        }

        i64 sys_waitpid(i32 pid, u64 status_ptr, u32 options) {
            // No user pointer ever reaches proc::waitpid: validate the
            // range up front (a bad pointer fails before we block), then
            // deliver the status through a kernel local and copy out
            // afterwards — waitpid blocks, so nothing may be held
            // across it, and the range is re-checked at delivery.
            const sys::user_range status =
                sys::user_range::bytes(status_ptr, sizeof(i32));
            if (!status.null() && !sys::user_range_ok(status)) {
                return -EFAULT;
            }
            i32       local  = 0;
            const i64 result = waitpid(pid, status.null() ? nullptr : &local, options);
            if (result >= 0 && !status.null() && !sys::copy_out(status, &local)) {
                return -EFAULT; // reaped, but undeliverable
            }
            return result;
        }

        /* spawnve's three arguments cross as user pointers: a path
         * string and two NULL-terminated pointer vectors, each element
         * of a vector another string.  Everything lands in one kernel
         * block before proc::spawnve sees it — spawnve never blocks, so
         * unlike waitpid there is nothing to copy out and no pointer to
         * re-check.  Bounds: 256 B per string, 32 elements per vector;
         * past that the vectors are rejected with -E2BIG rather than
         * walked forever. */
        enum : usize { path_cap = 256, arg_cap = 256, vec_max = 32 };

        i64 copy_in_vector(char** vec, char* pool, u64 user_vector) {
            const sys::user_range vector =
                sys::user_range::bytes(user_vector, vec_max * sizeof(char*));
            if (vector.null()) {
                vec[0] = nullptr;
                return 0; // a null vector means "empty"
            }
            for (usize i = 0; i < vec_max; ++i) {
                char* element = nullptr;
                if (!sys::copy_in(&element,
                                  vector.slice(i * sizeof(char*), sizeof element))) {
                    return -EFAULT;
                }
                if (element == nullptr) {
                    vec[i] = nullptr;
                    return 0;
                }
                // An element is a user address read out of user memory, so
                // it is exactly as untrusted as the vector that named it.
                if (!sys::copy_in_string(
                        pool, arg_cap,
                        sys::user_range::string(reinterpret_cast<u64>(element)))) {
                    return -EFAULT;
                }
                vec[i] = pool;
                pool += arg_cap;
            }
            return -E2BIG; // no NULL terminator within the bound
        }

        i64 sys_spawnve(u64 path_ptr, u64 argv_ptr, u64 envp_ptr) {
            char path[path_cap];
            if (!sys::copy_in_string(path, sizeof path,
                                     sys::user_range::string(path_ptr))) {
                return -EFAULT;
            }

            // One heap block: both pointer vectors, then the string
            // pool behind them — the kernel stack must not carry up to
            // 16 KB of copied arguments through the ELF-loading call
            // chain that follows.
            const usize vec_bytes  = 2 * (vec_max + 1) * sizeof(char*);
            const usize pool_bytes = 2 * vec_max * arg_cap;
            auto        storage    = mm::heap::alloc(vec_bytes + pool_bytes);
            if (!storage) {
                return -ENOMEM;
            }
            char** argv_vec = reinterpret_cast<char**>(storage.value);
            char** envp_vec = argv_vec + vec_max + 1;
            char*  pool     = reinterpret_cast<char*>(envp_vec + vec_max + 1);

            i64 result = copy_in_vector(argv_vec, pool, argv_ptr);
            if (result == 0) {
                result = copy_in_vector(envp_vec, pool + vec_max * arg_cap, envp_ptr);
            }
            if (result == 0) {
                int  error = -ENOMEM; // floor: spawnve sets one on every path
                auto child = spawnve(path, argv_vec, envp_vec, &error);
                result     = child ? static_cast<i64>(child->pid()) : error;
            }
            mm::heap::free(storage.value);
            return result;
        }

        i64 sys_execve(const arch::syscall_frame& frame) {
            using exec_logs      = astia::log::context<"execve">;
            auto*     exec_owner = current_process();
            const u64 path_ptr   = frame.a0;
            const u64 argv_ptr   = frame.a1;
            const u64 envp_ptr   = frame.a2;
            char      path[path_cap];
            if (!sys::copy_in_string(path, sizeof path,
                                     sys::user_range::string(path_ptr)))
                return -EFAULT;
            const usize vec_bytes  = 2 * (vec_max + 1) * sizeof(char*);
            const usize pool_bytes = 2 * vec_max * arg_cap;
            auto        storage    = mm::heap::alloc(vec_bytes + pool_bytes);
            if (!storage)
                return -ENOMEM;
            char** argv_vec = reinterpret_cast<char**>(storage.value);
            char** envp_vec = argv_vec + vec_max + 1;
            char*  pool     = reinterpret_cast<char*>(envp_vec + vec_max + 1);
            i64    result   = copy_in_vector(argv_vec, pool, argv_ptr);
            if (result == 0)
                result = copy_in_vector(envp_vec, pool + vec_max * arg_cap, envp_ptr);
            if (result == 0) {
                exec_logs::info("request pid=%d path=%s argv0=%s argv1=%s",
                                exec_owner ? static_cast<int>(exec_owner->pid()) : -1,
                                path, argv_vec[0] ? argv_vec[0] : "(null)",
                                argv_vec[1] ? argv_vec[1] : "(null)");
            }
            if (result == 0) {
                auto* owner = current_process();
                auto  file  = fs::open_path(path);
                if (owner == nullptr || !file)
                    result = -ENOENT;
                else {
                    const long size  = file->size();
                    u64        entry = 0, stack = 0;
                    if (size <= 0 ||
                        !owner->exec_user_file(file, static_cast<usize>(size), argv_vec,
                                               envp_vec, &entry, &stack)) {
                        exec_logs::error("load failed path=%s size=%ld", path, size);
                        result = -ENOEXEC;
                    }
                    else {
                        exec_logs::info("loaded path=%s entry=%llx stack=%llx tls=%llx",
                                        path, static_cast<unsigned long long>(entry),
                                        static_cast<unsigned long long>(stack),
                                        static_cast<unsigned long long>(owner->tls_tp()));
                        auto& next  = const_cast<arch::syscall_frame&>(frame);
                        next.rip    = entry;
                        next.rsp    = stack;
                        next.rflags = 0x202;
                        arch::context_set_fs_base(current_thread()->context(),
                                                  owner->tls_tp());
                        result = 0;
                    }
                }
            }
            mm::heap::free(storage.value);
            return result;
        }
        /// mmap — private memory in the caller's address space:
        /// anonymous, or a snapshot of an open file.  `addr` is a
        /// placement hint, never a forced address.  Never blocks.
        i64 sys_mmap(u64 addr, i64 len, u32 prot, u32 flags, i32 fd, i64 off) {
            auto* process = current_process();
            if (process == nullptr) {
                return -ESRCH;
            }
            if (len <= 0) {
                return -EINVAL;
            }
            if ((flags & MAP_ANONYMOUS) != 0) {
                // The anonymous form mandates fd = -1, offset = 0.
                if (fd != -1 || off != 0) {
                    return -EINVAL;
                }
                return static_cast<i64>(
                    process->map_anonymous(addr, static_cast<usize>(len), prot, flags));
            }
            if (fd < 0 || off < 0) {
                return -EINVAL;
            }
            return static_cast<i64>(process->map_file(
                fd, addr, static_cast<usize>(len), prot, flags, static_cast<u64>(off)));
        }

        /// munmap — remove [addr, addr + len) from the caller's
        /// address space.  Never blocks.
        i64 sys_munmap(u64 addr, i64 len) {
            auto* process = current_process();
            if (process == nullptr) {
                return -ESRCH;
            }
            if (len <= 0) {
                return -EINVAL;
            }
            return process->unmap(addr, static_cast<usize>(len));
        }

        /// fork — duplicate the caller copy-on-write.  The one handler
        /// that takes the trap image itself instead of its six argument
        /// registers: the child resumes at the caller's own ring-3
        /// instruction, so proc::fork wants the caller's whole register
        /// state.  Never blocks.
        i64 sys_fork(const arch::syscall_frame& frame) {
            return proc::fork(arch::caller_image(frame));
        }

        i64 sys_futex(u64 address, u32 op, u32 expected, u64 timeout) {
            const u32 operation = op & ~FUTEX_PRIVATE_FLAG;
            // Timeouts are deliberately deferred: an untimed wait/wake
            // is enough for the pthread join/lock substrate, while
            // silently ignoring one would violate callers' expectations.
            if (timeout != 0) {
                return -ENOSYS;
            }
            if (operation == FUTEX_WAIT) {
                return futex_wait(address, expected);
            }
            if (operation == FUTEX_WAKE) {
                return futex_wake(address, expected);
            }
            return -EINVAL;
        }

        i64 sys_thread_create(u64 entry, u64 stack_top, u64 argument) {
            auto* owner = current_process();
            if (owner == nullptr || entry == 0 || stack_top == 0 ||
                entry >= arch::kernel_base || stack_top >= arch::kernel_base ||
                (stack_top & 0xf) != 0 || stack_top < 16 ||
                !sys::user_range_ok(sys::user_range::bytes(entry, 1)) ||
                !sys::user_range_ok(sys::user_range::bytes(stack_top - 16, 16))) {
                return -EFAULT;
            }
            auto value = owner->create_thread(entry, stack_top, argument);
            return value ? static_cast<i64>(value->tid()) : -EAGAIN;
        }

        i64 sys_thread_create2(u64 entry, u64 stack_top, u64 argument0, u64 argument1) {
            auto* owner = current_process();
            if (owner == nullptr || entry == 0 || stack_top == 0 ||
                entry >= arch::kernel_base || stack_top >= arch::kernel_base ||
                (stack_top & 0xf) != 0 || stack_top < 16 ||
                !sys::user_range_ok(sys::user_range::bytes(entry, 1)) ||
                !sys::user_range_ok(sys::user_range::bytes(stack_top - 16, 16)) ||
                !sys::user_range_ok(sys::user_range::bytes(argument0, 1)) ||
                (argument1 != 0 &&
                 !sys::user_range_ok(sys::user_range::bytes(argument1, 1)))) {
                return -EFAULT;
            }
            auto value = owner->create_thread(entry, stack_top, argument0, argument1);
            return value ? static_cast<i64>(value->tid()) : -EAGAIN;
        }

        i64 sys_thread_exit(i64 result) {
            auto* self = current_thread();
            if (self == nullptr) {
                return -ESRCH;
            }
            self->exit(reinterpret_cast<void*>(static_cast<usize>(result)));
            __builtin_unreachable();
        }

        i64 sys_thread_join(u32 tid, u64 result_ptr) {
            auto* owner = current_process();
            if (owner == nullptr) {
                return -ESRCH;
            }
            const sys::user_range output =
                sys::user_range::bytes(result_ptr, sizeof(void*));
            if (!output.null() && !sys::user_range_ok(output)) {
                return -EFAULT;
            }
            auto target = owner->thread_by_tid(tid);
            if (!target) {
                return -ESRCH;
            }
            void*      result = nullptr;
            const long rc     = target->join(output.null() ? nullptr : &result);
            if (rc < 0) {
                return rc;
            }
            if (!output.null() && !sys::copy_out(output, &result)) {
                return -EFAULT;
            }
            return 0;
        }

        i64 sys_thread_detach(u32 tid) {
            auto* owner = current_process();
            if (owner == nullptr) {
                return -ESRCH;
            }
            auto target = owner->thread_by_tid(tid);
            return target ? target->detach() : -ESRCH;
        }

        /// The #PF path's repair offer for ring-3 faults: the process
        /// resolves its copy-on-write pages (fork's shared mappings);
        /// everything it declines stays fatal.
        bool user_fault(u64 addr, bool write) {
            auto* owner = current_process();
            return owner != nullptr && owner->cow_fault(addr, write);
        }
    }

    bool register_syscalls() {
        return sys::register_syscall<sys_getpid>(sys::syscall_id::getpid) &&
               sys::register_syscall<sys_gettid>(sys::syscall_id::gettid) &&
               sys::register_syscall<sys_kill>(sys::syscall_id::kill) &&
               sys::register_syscall<sys_sigaction>(sys::syscall_id::sigaction) &&
               sys::register_syscall<sys_sigreturn>(sys::syscall_id::sigreturn) &&
               sys::register_syscall<sys_sigprocmask>(sys::syscall_id::sigprocmask) &&
               sys::register_syscall<sys_yield>(sys::syscall_id::yield) &&
               sys::register_syscall<sys_exit>(sys::syscall_id::exit) &&
               sys::register_syscall<sys_waitpid>(sys::syscall_id::waitpid) &&
               sys::register_syscall<sys_spawnve>(sys::syscall_id::spawnve) &&
               sys::register_syscall<sys_execve>(sys::syscall_id::execve) &&
               sys::register_syscall<sys_sigsuspend>(sys::syscall_id::sigsuspend) &&
               sys::register_syscall<sys_getpgrp>(sys::syscall_id::getpgrp) &&
               sys::register_syscall<sys_setpgid>(sys::syscall_id::setpgid) &&
               sys::register_syscall<sys_setsid>(sys::syscall_id::setsid) &&
               sys::register_syscall<sys_mmap>(sys::syscall_id::mmap) &&
               sys::register_syscall<sys_munmap>(sys::syscall_id::munmap) &&
               sys::register_syscall<sys_fork>(sys::syscall_id::fork) &&
               sys::register_syscall<sys_futex>(sys::syscall_id::futex) &&
               sys::register_syscall<sys_thread_create>(sys::syscall_id::thread_create) &&
               sys::register_syscall<sys_thread_exit>(sys::syscall_id::thread_exit) &&
               sys::register_syscall<sys_thread_join>(sys::syscall_id::thread_join) &&
               sys::register_syscall<sys_thread_detach>(sys::syscall_id::thread_detach) &&
               sys::register_syscall<sys_thread_create2>(
                   sys::syscall_id::thread_create2) &&
               sys::register_exit_hook(deliver_default_signal) &&
               arch::register_user_fault_handler(user_fault);
    }

} // namespace astia::proc
