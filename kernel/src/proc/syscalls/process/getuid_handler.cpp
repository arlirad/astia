#include <syscall.hpp>

namespace astia::proc::syscalls {

    struct getuid_syscall : sys::syscall_handler<getuid_syscall, sys::syscall_id::getuid,
                                                 sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor getuid_descriptor = getuid_syscall::descriptor;

} // namespace astia::proc::syscalls
