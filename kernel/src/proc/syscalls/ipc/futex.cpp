#include "../../../ipc/futex.hpp"

#include <astia/api/aio.h>
#include <astia/api/errno.h>
#include <astia/api/syscalls.h>
#include <astia/arch/context.hpp>
#include <astia/arch/cpu.hpp>
#include <astia/fs.hpp>
#include <astia/log.hpp>
#include <astia/mm/heap.hpp>

#include "../../../ipc/mapping.hpp"
#include "../../mapping.hpp"
#include "../../proc.hpp"
#include "../../scheduler.hpp"
#include "syscall.hpp"
#include "user_mem.hpp"

#include <syscall.hpp>
namespace astia::proc {
    i64 futex_wait(u64 address, u32 expected, u64 timeout, bool cancellation_point,
                   bool shared);
    i64 futex_wake(u64 address, u32 count, bool shared);
}

namespace astia::proc::syscalls {

    /// futex - Wait on or wake a shared user word.
    ///
    /// Synopsis:
    ///     long futex(uint32_t *address, uint32_t operation, uint32_t value, const
    ///     timespec *timeout);
    ///
    /// Description:
    ///     FUTEX_WAIT blocks while the word equals value. FUTEX_WAKE wakes up
    ///     up to value waiters. The private flag selects an address key owned
    ///     by the calling process; without it, a MAP_SHARED backing key is used.
    ///
    /// Return Value:
    ///     Wait returns zero; wake returns the number of woken waiters.
    ///
    /// Errors:
    ///     -EAGAIN: The word no longer equals the expected value.
    ///     -EFAULT: The futex word is not accessible userspace memory.
    ///     -EINVAL: The operation is unknown.
    ///     -ETIMEDOUT: The absolute CLOCK_MONOTONIC timeout elapsed.
    struct futex
        : sys::syscall_handler<futex, sys::syscall_id::futex, sys::syscall_mode::sync> {
        static i64 invoke(sys::user_ptr<u32> address, u32 op, u32 expected,
                          sys::user_ptr<astia_timespec> timeout) {
            const bool cancellation_point = (op & ipc::futex_cancelable_bit) != 0;
            auto       operation          = mapper<ipc::futex_operation>::from(
                op & ~(ipc::futex_private_bit | ipc::futex_cancelable_bit));
            if (!operation)
                return -EINVAL;
            const bool shared = (op & ipc::futex_private_bit) == 0;
            if (operation.value == ipc::futex_operation::wait) {
                return proc::futex_wait(address.address(), expected, timeout.address(),
                                        cancellation_point, shared);
            }
            if (operation.value == ipc::futex_operation::wake) {
                if (!timeout.null())
                    return -EINVAL;
                return proc::futex_wake(address.address(), expected, shared);
            }
            return -EINVAL;
        }
    };

    extern const sys::syscall_descriptor futex_descriptor = futex::descriptor;

} // namespace astia::proc::syscalls
