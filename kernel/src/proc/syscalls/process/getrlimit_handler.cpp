#include <syscall.hpp>

namespace astia::proc::syscalls {

    struct getrlimit_syscall
        : sys::syscall_handler<getrlimit_syscall, sys::syscall_id::getrlimit,
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor getrlimit_descriptor =
        getrlimit_syscall::descriptor;

} // namespace astia::proc::syscalls
