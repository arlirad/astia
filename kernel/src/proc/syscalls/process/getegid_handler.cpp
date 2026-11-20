#include <syscall.hpp>

namespace astia::proc::syscalls {

    struct getegid_syscall
        : sys::syscall_handler<getegid_syscall, sys::syscall_id::getegid,
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor getegid_descriptor = getegid_syscall::descriptor;

} // namespace astia::proc::syscalls
