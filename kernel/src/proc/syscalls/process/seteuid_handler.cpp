#include <syscall.hpp>

namespace astia::proc::syscalls {

    struct seteuid_syscall
        : sys::syscall_handler<seteuid_syscall, sys::syscall_id::seteuid,
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor seteuid_descriptor = seteuid_syscall::descriptor;

} // namespace astia::proc::syscalls
