#include <astia/api/errno.h>
#include <astia/api/syscalls.h>
#include "interrupts.hpp"
#include "syscall.hpp"
#include "user_mem.hpp"
#include "heap.hpp"
#include "proc.hpp"
#include "scheduler.hpp"
#include "log.hpp"

namespace astia::proc {

    auto futex_wait(u64 address, u32 expected) -> i64;
    auto futex_wake(u64 address, u32 count) -> i64;

    namespace {
        auto sys_getpid(i64, i64, i64, i64, i64, i64) -> i64 {
            auto* process = current_process();
            return process == nullptr ? -ESRCH : static_cast<i64>(process->pid());
        }

        auto sys_gettid(i64, i64, i64, i64, i64, i64) -> i64 {
            auto* thread = current_thread();
            return thread == nullptr ? -ESRCH : static_cast<i64>(thread->tid());
        }

        auto sys_yield(i64, i64, i64, i64, i64, i64) -> i64 {
            scheduler::yield();
            return 0;
        }

        auto sys_exit(i64 status, i64, i64, i64, i64, i64) -> i64 {
            auto* thread = current_thread();
            if (thread == nullptr) return -ESRCH;
            log::info("user thread exit status=%ld", status);
            thread->exit(reinterpret_cast<void*>(static_cast<usize>(status)));
            __builtin_unreachable();
        }

        auto sys_waitpid(i64 pid, i64 status_ptr, i64, i64, i64, i64) -> i64 {
            // No user pointer ever reaches proc::waitpid: validate the
            // range up front (a bad pointer fails before we block), then
            // deliver the status through a kernel local and copy out
            // afterwards — waitpid blocks, so nothing may be held
            // across it, and the range is re-checked at delivery.
            auto* status = reinterpret_cast<void*>(static_cast<usize>(status_ptr));
            if (status != nullptr && !sys::user_range_ok(status, sizeof(i32))) {
                return -EFAULT;
            }
            i32   local  = 0;
            const i64 result =
                waitpid(static_cast<i32>(pid), status == nullptr ? nullptr : &local);
            if (result >= 0 && status != nullptr &&
                !sys::copy_out(status, &local, sizeof(local))) {
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

        auto copy_in_vector(char** vec, char* pool, i64 user_vector) -> i64 {
            if (user_vector == 0) {
                vec[0] = nullptr;
                return 0; // a null vector means "empty"
            }
            for (usize i = 0; i < vec_max; ++i) {
                char* element = nullptr;
                const auto* slot = reinterpret_cast<const void*>(
                    static_cast<usize>(user_vector) + i * sizeof(char*));
                if (!sys::copy_in(&element, slot, sizeof element)) {
                    return -EFAULT;
                }
                if (element == nullptr) {
                    vec[i] = nullptr;
                    return 0;
                }
                if (!sys::copy_in_string(pool, arg_cap, element)) {
                    return -EFAULT;
                }
                vec[i] = pool;
                pool += arg_cap;
            }
            return -E2BIG; // no NULL terminator within the bound
        }

        auto sys_spawnve(i64 path_ptr, i64 argv_ptr, i64 envp_ptr, i64, i64, i64)
            -> i64 {
            char path[path_cap];
            if (!sys::copy_in_string(path, sizeof path,
                                     reinterpret_cast<const void*>(
                                         static_cast<usize>(path_ptr)))) {
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
            char*  pool = reinterpret_cast<char*>(envp_vec + vec_max + 1);

            i64 result = copy_in_vector(argv_vec, pool, argv_ptr);
            if (result == 0) {
                result = copy_in_vector(envp_vec, pool + vec_max * arg_cap, envp_ptr);
            }
            if (result == 0) {
                int  error = -ENOMEM; // floor: spawnve sets one on every path
                auto child = spawnve(path, argv_vec, envp_vec, &error);
                result = child ? static_cast<i64>(child->pid()) : error;
            }
            mm::heap::free(storage.value);
            return result;
        }
        /// mmap — private memory in the caller's address space:
        /// anonymous, or a snapshot of an open file.  `addr` is a
        /// placement hint, never a forced address.  Never blocks.
        auto sys_mmap(i64 addr, i64 len, i64 prot, i64 flags, i64 fd,
                      i64 off) -> i64 {
            auto* process = current_process();
            if (process == nullptr) {
                return -ESRCH;
            }
            if (len <= 0) {
                return -EINVAL;
            }
            if ((static_cast<u32>(flags) & MAP_ANONYMOUS) != 0) {
                // The anonymous form mandates fd = -1, offset = 0.
                if (fd != -1 || off != 0) {
                    return -EINVAL;
                }
                return static_cast<i64>(
                    process->map_anonymous(static_cast<u64>(addr),
                                           static_cast<usize>(len),
                                           static_cast<u32>(prot),
                                           static_cast<u32>(flags)));
            }
            if (fd < 0 || off < 0) {
                return -EINVAL;
            }
            return static_cast<i64>(
                process->map_file(static_cast<i32>(fd), static_cast<u64>(addr),
                                  static_cast<usize>(len),
                                  static_cast<u32>(prot),
                                  static_cast<u32>(flags),
                                  static_cast<u64>(off)));
        }

        /// munmap — remove [addr, addr + len) from the caller's
        /// address space.  Never blocks.
        auto sys_munmap(i64 addr, i64 len, i64, i64, i64, i64) -> i64 {
            auto* process = current_process();
            if (process == nullptr) {
                return -ESRCH;
            }
            if (len <= 0) {
                return -EINVAL;
            }
            return process->unmap(static_cast<u64>(addr),
                                  static_cast<usize>(len));
        }

        /// fork — duplicate the caller copy-on-write.  The dispatch
        /// shim packs the caller's register image and passes it as
        /// a0: the child resumes the parent's ring-3 call site with
        /// rax = 0.  Never blocks.
        auto sys_fork(i64 uctx, i64, i64, i64, i64, i64) -> i64 {
            if (uctx == 0) {
                return -EFAULT;
            }
            return proc::fork(reinterpret_cast<const arch::user_context*>(uctx));
        }

        auto sys_futex(i64 address, i64 op, i64 expected, i64 timeout,
                       i64, i64) -> i64 {
            const u32 operation = static_cast<u32>(op) & ~FUTEX_PRIVATE_FLAG;
            // Timeouts are deliberately deferred: an untimed wait/wake
            // is enough for the pthread join/lock substrate, while
            // silently ignoring one would violate callers' expectations.
            if (timeout != 0) {
                return -ENOSYS;
            }
            if (operation == FUTEX_WAIT) {
                return futex_wait(static_cast<u64>(address),
                                  static_cast<u32>(expected));
            }
            if (operation == FUTEX_WAKE) {
                return futex_wake(static_cast<u64>(address),
                                  static_cast<u32>(expected));
            }
            return -EINVAL;
        }

        auto sys_thread_create(i64 entry, i64 stack_top, i64 argument,
                               i64, i64, i64) -> i64 {
            auto* owner = current_process();
            if (owner == nullptr || entry <= 0 || stack_top <= 0 ||
                static_cast<u64>(entry) >= arch::kernel_base ||
                static_cast<u64>(stack_top) >= arch::kernel_base ||
                (static_cast<u64>(stack_top) & 0xf) != 0 ||
                stack_top < 16 ||
                !sys::user_range_ok(reinterpret_cast<void*>(static_cast<u64>(entry)),
                                    1) ||
                !sys::user_range_ok(
                    reinterpret_cast<void*>(static_cast<u64>(stack_top) - 16),
                    16)) {
                return -EFAULT;
            }
            auto value = owner->create_thread(static_cast<u64>(entry),
                                              static_cast<u64>(stack_top),
                                              static_cast<u64>(argument));
            return value ? static_cast<i64>(value->tid()) : -EAGAIN;
        }

        auto sys_thread_exit(i64 result, i64, i64, i64, i64, i64) -> i64 {
            auto* self = current_thread();
            if (self == nullptr) {
                return -ESRCH;
            }
            self->exit(reinterpret_cast<void*>(static_cast<usize>(result)));
            __builtin_unreachable();
        }

        auto sys_thread_join(i64 tid, i64 result_ptr, i64, i64, i64, i64) -> i64 {
            auto* owner = current_process();
            if (owner == nullptr) {
                return -ESRCH;
            }
            auto* output = reinterpret_cast<void*>(static_cast<usize>(result_ptr));
            if (output != nullptr && !sys::user_range_ok(output, sizeof(void*))) {
                return -EFAULT;
            }
            auto target = owner->thread_by_tid(static_cast<tid_t>(tid));
            if (!target) {
                return -ESRCH;
            }
            void* result = nullptr;
            const long rc = target->join(output == nullptr ? nullptr : &result);
            if (rc < 0) {
                return rc;
            }
            if (output != nullptr && !sys::copy_out(output, &result, sizeof(result))) {
                return -EFAULT;
            }
            return 0;
        }

        auto sys_thread_detach(i64 tid, i64, i64, i64, i64, i64) -> i64 {
            auto* owner = current_process();
            if (owner == nullptr) {
                return -ESRCH;
            }
            auto target = owner->thread_by_tid(static_cast<tid_t>(tid));
            return target ? target->detach() : -ESRCH;
        }

        /// The #PF path's repair offer for ring-3 faults: the process
        /// resolves its copy-on-write pages (fork's shared mappings);
        /// everything it declines stays fatal.
        auto user_fault(u64 addr, bool write) -> bool {
            auto* owner = current_process();
            return owner != nullptr && owner->cow_fault(addr, write);
        }
    }

    auto register_syscalls() -> bool {
        return sys::register_syscall(sys::syscall_id::getpid, sys_getpid) &&
               sys::register_syscall(sys::syscall_id::gettid, sys_gettid) &&
               sys::register_syscall(sys::syscall_id::yield, sys_yield) &&
               sys::register_syscall(sys::syscall_id::exit, sys_exit) &&
               sys::register_syscall(sys::syscall_id::waitpid, sys_waitpid) &&
               sys::register_syscall(sys::syscall_id::spawnve, sys_spawnve) &&
               sys::register_syscall(sys::syscall_id::mmap, sys_mmap) &&
               sys::register_syscall(sys::syscall_id::munmap, sys_munmap) &&
               sys::register_syscall(sys::syscall_id::fork, sys_fork) &&
               sys::register_syscall(sys::syscall_id::futex, sys_futex) &&
               sys::register_syscall(sys::syscall_id::thread_create,
                                     sys_thread_create) &&
               sys::register_syscall(sys::syscall_id::thread_exit,
                                     sys_thread_exit) &&
               sys::register_syscall(sys::syscall_id::thread_join,
                                     sys_thread_join) &&
               sys::register_syscall(sys::syscall_id::thread_detach,
                                     sys_thread_detach) &&
               arch::register_user_fault_handler(user_fault);
    }

} // namespace astia::proc
