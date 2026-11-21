#include <astia/mapping.hpp>

#include "../../mapping.hpp"
#include "../../proc.hpp"

#include <syscall.hpp>

namespace astia::proc::syscall {
    i64 getsid(i64 raw_target) {
        const auto target = mapper<proc::getsid_target>::from(raw_target);
        return target ? proc::getsid(target.value.pid) : -ESRCH;
    }
}

namespace astia::proc::syscalls {
    struct getsid : sys::syscall_handler<getsid, sys::syscall_id::getsid,
                                         sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::invoke_sync<&::astia::proc::syscall::getsid>(context);
        }
    };

    extern const sys::syscall_descriptor getsid_descriptor = getsid::descriptor;

} // namespace astia::proc::syscalls
