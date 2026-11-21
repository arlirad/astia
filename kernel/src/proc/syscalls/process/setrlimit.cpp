#include "rlimit.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {
    struct setrlimit : sys::syscall_handler<setrlimit, sys::syscall_id::setrlimit,
                                            sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::invoke_sync<&::astia::proc::syscall::setrlimit>(context);
        }
    };

    extern const sys::syscall_descriptor setrlimit_descriptor = setrlimit::descriptor;

} // namespace astia::proc::syscalls
