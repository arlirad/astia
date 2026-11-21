#include <astia/api/errno.h>

#include "../../proc.hpp"
#include "user_mem.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {
    struct setgid
        : sys::syscall_handler<setgid, sys::syscall_id::setgid, sys::syscall_mode::sync> {
        static i64 invoke(sys::syscall_context& context) {
            i64   v = static_cast<i64>(context.arguments[0]);
            auto* p = current_process();
            if (!p)
                return -ESRCH;
            if (v < 0 || static_cast<u64>(v) > ~u32{0})
                return -EINVAL;
            return p->setgid(static_cast<u32>(v));
        }
    };

    extern const sys::syscall_descriptor setgid_descriptor = setgid::descriptor;

} // namespace astia::proc::syscalls
