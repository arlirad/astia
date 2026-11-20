#include <syscall.hpp>

namespace astia::proc::syscalls {

    struct setegid_syscall
        : sys::syscall_handler<setegid_syscall, sys::syscall_id::setegid,
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor setegid_descriptor = setegid_syscall::descriptor;

} // namespace astia::proc::syscalls
