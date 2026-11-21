#include "credentials.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {
    struct setreuid : sys::syscall_handler<setreuid, sys::syscall_id::setreuid,
                                           sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::invoke_sync<&::astia::proc::syscall::setreuid>(context);
        }
    };

    extern const sys::syscall_descriptor setreuid_descriptor = setreuid::descriptor;

} // namespace astia::proc::syscalls
