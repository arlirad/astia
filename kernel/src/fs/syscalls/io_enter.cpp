#include "io_enter.hpp"

#include "../../sys/aio.hpp"
#include "../../sys/user_mem.hpp"
#include <astia/api/aio.h>
#include <astia/api/errno.h>
namespace astia::fs::syscall {
    i64 io_enter(u64 ring, u32 submit, u32 complete, u32 flags, u64 timeout) {
        const sys::user_ptr<astia_timespec> user_timeout(timeout);
        astia_timespec timeout_copy{};
        const auto validated = user_timeout.null() ? optional<sys::validated_user_ptr<astia_timespec>>{}
                                                    : user_timeout.validate();
        if (!user_timeout.null() && (!validated ||
                                     !sys::copy_in(&timeout_copy, validated.value.bytes())))
            return -EFAULT;
        return sys::io_enter_abi(reinterpret_cast<astia_rings*>(static_cast<usize>(ring)),
                                 submit, complete, flags,
                                  user_timeout.null() ? nullptr : &timeout_copy);
    }
}
