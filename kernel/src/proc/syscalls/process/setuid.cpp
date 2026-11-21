#include "credentials.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {
    struct setuid : sys::syscall_handler<setuid, sys::syscall_id::setuid,
                                         sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::invoke_sync<&::astia::proc::syscall::setuid>(context);
        }
    };

    extern const sys::syscall_descriptor setuid_descriptor = setuid::descriptor;

} // namespace astia::proc::syscalls
