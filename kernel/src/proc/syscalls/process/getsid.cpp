#include <astia/mapping.hpp>

#include "../../mapping.hpp"
#include "../../proc.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {
    struct getsid
        : sys::syscall_handler<getsid, sys::syscall_id::getsid, sys::syscall_mode::sync> {
        static i64 invoke(i64 raw_target) {
            const auto target = mapper<proc::getsid_target>::from(raw_target);
            return target ? proc::getsid(target.value.pid) : -ESRCH;
        }
    };

    extern const sys::syscall_descriptor getsid_descriptor = getsid::descriptor;

} // namespace astia::proc::syscalls
