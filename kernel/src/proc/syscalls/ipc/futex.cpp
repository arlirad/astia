#include "futex.hpp"

#include "../../../ipc/futex.hpp"
#include "../../../ipc/mapping.hpp"
#include "../../mapping.hpp"
#include "../../proc.hpp"
#include "../../scheduler.hpp"
#include "syscall.hpp"
#include "user_mem.hpp"
#include <astia/api/aio.h>
#include <astia/api/errno.h>
#include <astia/api/syscalls.h>
#include <astia/arch/context.hpp>
#include <astia/arch/cpu.hpp>
#include <astia/fs.hpp>
#include <astia/log.hpp>
#include <astia/mm/heap.hpp>
namespace astia::proc {
    i64 futex_wait(u64 address, u32 expected, u64 timeout, bool cancellation_point);
    i64 futex_wake(u64 address, u32 count);
}

namespace astia::proc::syscall {

    i64 futex(u64 address, u32 op, u32 expected, u64 timeout) {
        const bool cancellation_point = (op & ipc::futex_cancelable_bit) != 0;
        auto operation = mapper<ipc::futex_operation>::from(
            op & ~(ipc::futex_private_bit | ipc::futex_cancelable_bit));
        if (!operation)
            return -EINVAL;
        if (operation.value == ipc::futex_operation::wait) {
            return proc::futex_wait(address, expected, timeout, cancellation_point);
        }
        if (operation.value == ipc::futex_operation::wake) {
            if (timeout != 0)
                return -EINVAL;
            return proc::futex_wake(address, expected);
        }
        return -EINVAL;
    }
}
