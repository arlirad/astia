#include <astia/api/aio.h>
#include <astia/api/errno.h>
#include <astia/api/syscalls.h>
#include <astia/arch/context.hpp>
#include <astia/arch/cpu.hpp>
#include <astia/fs.hpp>
#include <astia/log.hpp>
#include <astia/mm/heap.hpp>

#include "../../../ipc/futex.hpp"
#include "../../../ipc/mapping.hpp"
#include "../../mapping.hpp"
#include "../../proc.hpp"
#include "../../scheduler.hpp"
#include "syscall.hpp"
#include "user_mem.hpp"

#include <syscall.hpp>
namespace astia::proc::syscalls {

    /// thread_exit - Terminate the calling user thread.
    ///
    /// Synopsis:
    ///     long thread_exit(long result);
    ///
    /// Description:
    ///     Terminates the calling thread and records result for a joining
    ///     thread. A process becomes waitable after its last thread exits.
    ///
    /// Return Value:
    ///     Does not return.
    struct thread_exit : sys::syscall_handler<thread_exit, sys::syscall_id::thread_exit,
                                              sys::syscall_mode::sync> {
        static i64 invoke(i64 result) {
            auto* self = current_thread();
            if (self == nullptr) {
                return -ESRCH;
            }
            self->exit(reinterpret_cast<void*>(static_cast<usize>(result)));
            __builtin_unreachable();
        }
    };

    extern const sys::syscall_descriptor thread_exit_descriptor = thread_exit::descriptor;

} // namespace astia::proc::syscalls
