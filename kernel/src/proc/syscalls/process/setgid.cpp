#include "credentials.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {
    struct setgid : sys::syscall_handler<setgid, sys::syscall_id::setgid,
                                         sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::invoke_sync<&::astia::proc::syscall::setgid>(context);
        }
    };

    extern const sys::syscall_descriptor setgid_descriptor = setgid::descriptor;

} // namespace astia::proc::syscalls
