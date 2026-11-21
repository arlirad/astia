#include <astia/api/errno.h>

#include "../../proc.hpp"
#include "user_mem.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {
    struct setregid : sys::syscall_handler<setregid, sys::syscall_id::setregid,
                                           sys::syscall_mode::sync> {
        static i64 invoke(sys::syscall_context& context) {
            i64   r = static_cast<i64>(context.arguments[0]);
            i64   e = static_cast<i64>(context.arguments[1]);
            auto* p = current_process();
            if (!p)
                return -ESRCH;
            if ((r < -1 || static_cast<u64>(r) > ~u32{0}) ||
                (e < -1 || static_cast<u64>(e) > ~u32{0}))
                return -EINVAL;
            return p->setregid(static_cast<u32>(r), static_cast<u32>(e));
        }
    };

    extern const sys::syscall_descriptor setregid_descriptor = setregid::descriptor;

} // namespace astia::proc::syscalls
