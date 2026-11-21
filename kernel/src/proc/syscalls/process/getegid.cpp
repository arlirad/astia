#include "credentials.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {
    struct getegid : sys::syscall_handler<getegid, sys::syscall_id::getegid,
                                          sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::invoke_sync<&::astia::proc::syscall::getegid>(context);
        }
    };

    extern const sys::syscall_descriptor getegid_descriptor = getegid::descriptor;

} // namespace astia::proc::syscalls
