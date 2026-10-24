#include "io_enter.hpp"
#include "../../sys/aio.hpp"
#include "../../sys/user_mem.hpp"
#include <astia/api/aio.h>
#include <astia/api/errno.h>
namespace astia::fs::syscall {
    i64 io_enter(u64 ring, u32 submit, u32 complete, u32 flags, u64 timeout) {
        const sys::user_range user_timeout =
            sys::user_range::bytes(timeout, sizeof(astia_timespec));
        astia_timespec timeout_copy{};
        if (!user_timeout.null() && !sys::copy_in(&timeout_copy, user_timeout)) return -EFAULT;
        return sys::io_enter_abi(
            reinterpret_cast<astia_rings*>(static_cast<usize>(ring)), submit, complete, flags,
            user_timeout.null() ? nullptr : &timeout_copy);
    }
}
