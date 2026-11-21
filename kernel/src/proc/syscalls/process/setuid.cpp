#include <astia/api/errno.h>

#include "../../proc.hpp"
#include "user_mem.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {
    struct setuid
        : sys::syscall_handler<setuid, sys::syscall_id::setuid, sys::syscall_mode::sync> {
        static i64 invoke(i64 v) {
            auto* p = current_process();
            if (!p)
                return -ESRCH;
            if (v < 0 || static_cast<u64>(v) > ~u32{0})
                return -EINVAL;
            return p->setuid(static_cast<u32>(v));
        }
    };

    extern const sys::syscall_descriptor setuid_descriptor = setuid::descriptor;

} // namespace astia::proc::syscalls
