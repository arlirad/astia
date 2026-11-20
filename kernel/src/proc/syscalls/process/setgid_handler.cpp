#include <syscall.hpp>

namespace astia::proc::syscalls {

    struct setgid_syscall : sys::syscall_handler<setgid_syscall, sys::syscall_id::setgid,
                                                 sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor setgid_descriptor = setgid_syscall::descriptor;

} // namespace astia::proc::syscalls
