#include "futex.hpp"
#include "../../proc.hpp"
#include "../../mapping.hpp"
#include "../../scheduler.hpp"
#include "user_mem.hpp"
#include "syscall.hpp"
#include "../../../ipc/futex.hpp"
#include "../../../ipc/mapping.hpp"
#include <astia/api/aio.h>
#include <astia/api/errno.h>
#include <astia/api/syscalls.h>
#include <astia/arch/context.hpp>
#include <astia/arch/cpu.hpp>
#include <astia/fs.hpp>
#include <astia/log.hpp>
#include <astia/mm/heap.hpp>
namespace astia::proc {
    i64 futex_wait(u64 address, u32 expected);
    i64 futex_wake(u64 address, u32 count);
}

namespace astia::proc::syscall {

        i64 futex(u64 address, u32 op, u32 expected, u64 timeout) {
            auto operation = mapper<ipc::futex_operation>::from(op & ~FUTEX_PRIVATE_FLAG);
            if (!operation) return -EINVAL;
            // Timeouts are deliberately deferred: an untimed wait/wake
            // is enough for the pthread join/lock substrate, while
            // silently ignoring one would violate callers' expectations.
            if (timeout != 0) {
                return -ENOSYS;
            }
            if (operation.value == ipc::futex_operation::wait) {
                return proc::futex_wait(address, expected);
            }
            if (operation.value == ipc::futex_operation::wake) {
                return proc::futex_wake(address, expected);
            }
            return -EINVAL;
        }
}
