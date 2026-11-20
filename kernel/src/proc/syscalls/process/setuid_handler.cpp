#include <syscall.hpp>

namespace astia::proc::syscalls {

    struct setuid_syscall : sys::syscall_handler<setuid_syscall, sys::syscall_id::setuid,
                                                 sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor setuid_descriptor = setuid_syscall::descriptor;

} // namespace astia::proc::syscalls
