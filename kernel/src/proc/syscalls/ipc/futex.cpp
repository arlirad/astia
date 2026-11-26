#include "../../../ipc/futex.hpp"

#include <astia/api/aio.h>
#include <astia/api/errno.h>
#include <astia/api/syscalls.h>
#include <astia/arch/context.hpp>
#include <astia/arch/cpu.hpp>
#include <astia/error.hpp>
#include <astia/fs.hpp>
#include <astia/mm/heap.hpp>

#include "../../../ipc/mapping.hpp"
#include "../../mapping.hpp"
#include "../../proc.hpp"
#include "../../scheduler.hpp"
#include "syscall.hpp"
#include "user_mem.hpp"

#include <syscall.hpp>
namespace astia::proc {
    core::task<i64> futex_wait(u64 address, u32 expected, u64 timeout,
                               bool cancellation_point, bool shared);
    i64             futex_wake(u64 address, u32 count, bool shared);
}

namespace astia::proc::syscalls {
    struct futex
        : sys::syscall_handler<futex, sys::syscall_id::futex, sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::user_ptr<u32> address, u32 op, u32 expected,
                                      sys::user_ptr<astia_timespec> timeout) {
            const bool cancellation_point = (op & ipc::futex_cancelable_bit) != 0;

            auto operation = mapper<ipc::futex_operation>::from(
                op & ~(ipc::futex_private_bit | ipc::futex_cancelable_bit));
            if (!operation)
                co_return -EINVAL;

            const bool shared = (op & ipc::futex_private_bit) == 0;

            if (operation.value == ipc::futex_operation::wait) {
                auto waited = co_await proc::futex_wait(address.address(), expected,
                                                        timeout.address(),
                                                        cancellation_point, shared);
                if (!waited)
                    co_return error_errno(waited.error);

                co_return waited.value;
            }

            if (operation.value == ipc::futex_operation::wake) {
                if (!timeout.null())
                    co_return -EINVAL;

                co_return proc::futex_wake(address.address(), expected, shared);
            }

            co_return -EINVAL;
        }
    };
} // namespace astia::proc::syscalls
