#include <astia/api/errno.h>

#include "../../proc.hpp"
#include "user_mem.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {
    struct seteuid : sys::syscall_handler<seteuid, sys::syscall_id::seteuid,
                                          sys::syscall_mode::sync> {
        static i64 invoke(sys::syscall_context& context) {
            i64   v = static_cast<i64>(context.arguments[0]);
            auto* p = current_process();
            if (!p)
                return -ESRCH;
            if (v < 0 || static_cast<u64>(v) > ~u32{0})
                return -EINVAL;
            return p->seteuid(static_cast<u32>(v));
        }
    };

    extern const sys::syscall_descriptor seteuid_descriptor = seteuid::descriptor;

} // namespace astia::proc::syscalls
