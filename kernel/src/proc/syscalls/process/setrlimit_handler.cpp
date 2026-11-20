#include <syscall.hpp>

namespace astia::proc::syscalls {

    struct setrlimit_syscall
        : sys::syscall_handler<setrlimit_syscall, sys::syscall_id::setrlimit,
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor setrlimit_descriptor =
        setrlimit_syscall::descriptor;

} // namespace astia::proc::syscalls
