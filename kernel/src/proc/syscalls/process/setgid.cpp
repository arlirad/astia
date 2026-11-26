#include <astia/api/errno.h>

#include "../../proc.hpp"
#include "user_mem.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {
    struct setgid
        : sys::syscall_handler<setgid, sys::syscall_id::setgid, sys::syscall_mode::sync> {
        static i64 invoke(i64 v) {
            auto* p = current_process();
            if (!p)
                return -ESRCH;

            if (v < 0 || static_cast<u64>(v) > ~u32{0})
                return -EINVAL;

            return p->setgid(static_cast<u32>(v));
        }
    };
} // namespace astia::proc::syscalls
