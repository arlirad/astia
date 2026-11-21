#include "credentials.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {
    struct setegid : sys::syscall_handler<setegid, sys::syscall_id::setegid,
                                          sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::invoke_sync<&::astia::proc::syscall::setegid>(context);
        }
    };

    extern const sys::syscall_descriptor setegid_descriptor = setegid::descriptor;

} // namespace astia::proc::syscalls
