#include <syscall.hpp>

namespace astia::proc::syscalls {

    struct setreuid_syscall
        : sys::syscall_handler<setreuid_syscall, sys::syscall_id::setreuid,
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor setreuid_descriptor =
        setreuid_syscall::descriptor;

} // namespace astia::proc::syscalls
