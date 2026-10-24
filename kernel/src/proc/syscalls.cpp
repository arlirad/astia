#include "proc.hpp"
#include "mapping.hpp"
#include "syscalls/kill.hpp"
#include "syscalls/getpid.hpp"
#include "syscalls/gettid.hpp"
#include "syscalls/getpgrp.hpp"
#include "syscalls/setpgid.hpp"
#include "syscalls/setsid.hpp"
#include "syscalls/signal/sigaction.hpp"
#include "syscalls/signal/sigreturn.hpp"
#include "syscalls/signal/sigprocmask.hpp"
#include "syscalls/signal/sigsuspend.hpp"
#include "../ipc/futex.hpp"
#include "../ipc/mapping.hpp"
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
            auto mapped_options = mapper<wait_options>::from(options);
            if (!mapped_options) return -EINVAL;
            const i64 result = waitpid(pid, status.null() ? nullptr : &local,
                                       mapped_options.value);
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
                auto child = spawnve(path, argv_vec, envp_vec);
                result = child ? static_cast<i64>(child.value->pid())
                               : -astia::errno_value(child.error);
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
                auto mapped_prot = mapper<map_protection>::from(prot);
                auto mapped_flags = mapper<map_flags>::from(flags);
                if (!mapped_prot || !mapped_flags) return -EINVAL;
                return static_cast<i64>(process->map_anonymous(
                    addr, static_cast<usize>(len), mapped_prot.value, mapped_flags.value));
            }
            if (fd < 0 || off < 0) {
                return -EINVAL;
            }
            auto mapped_fd = mapper<fd_t>::from(fd);
            if (!mapped_fd) return -EBADF;
            auto mapped_prot = mapper<map_protection>::from(prot);
            auto mapped_flags = mapper<map_flags>::from(flags);
            if (!mapped_prot || !mapped_flags) return -EINVAL;
            return static_cast<i64>(process->map_file(
                mapped_fd.value, addr, static_cast<usize>(len), mapped_prot.value, mapped_flags.value,
                static_cast<u64>(off)));
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
            auto operation = mapper<ipc::futex_operation>::from(op & ~FUTEX_PRIVATE_FLAG);
            if (!operation) return -EINVAL;
            // Timeouts are deliberately deferred: an untimed wait/wake
            // is enough for the pthread join/lock substrate, while
            // silently ignoring one would violate callers' expectations.
            if (timeout != 0) {
                return -ENOSYS;
            }
            if (operation.value == ipc::futex_operation::wait) {
                return futex_wait(address, expected);
            }
            if (operation.value == ipc::futex_operation::wake) {
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
         return sys::register_syscall<syscall::getpid>(sys::syscall_id::getpid) &&
                sys::register_syscall<syscall::gettid>(sys::syscall_id::gettid) &&
               sys::register_syscall<syscall::kill>(sys::syscall_id::kill) &&
                sys::register_syscall<syscall::sigaction>(sys::syscall_id::sigaction) &&
                sys::register_syscall<syscall::sigreturn>(sys::syscall_id::sigreturn) &&
                sys::register_syscall<syscall::sigprocmask>(sys::syscall_id::sigprocmask) &&
               sys::register_syscall<sys_yield>(sys::syscall_id::yield) &&
               sys::register_syscall<sys_exit>(sys::syscall_id::exit) &&
               sys::register_syscall<sys_waitpid>(sys::syscall_id::waitpid) &&
               sys::register_syscall<sys_spawnve>(sys::syscall_id::spawnve) &&
               sys::register_syscall<sys_execve>(sys::syscall_id::execve) &&
                sys::register_syscall<syscall::sigsuspend>(sys::syscall_id::sigsuspend) &&
                sys::register_syscall<syscall::getpgrp>(sys::syscall_id::getpgrp) &&
                sys::register_syscall<syscall::setpgid>(sys::syscall_id::setpgid) &&
                sys::register_syscall<syscall::setsid>(sys::syscall_id::setsid) &&
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
