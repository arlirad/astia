#include "credentials.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {
    struct setregid : sys::syscall_handler<setregid, sys::syscall_id::setregid,
                                           sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::invoke_sync<&::astia::proc::syscall::setregid>(context);
        }
    };

    extern const sys::syscall_descriptor setregid_descriptor = setregid::descriptor;

} // namespace astia::proc::syscalls
