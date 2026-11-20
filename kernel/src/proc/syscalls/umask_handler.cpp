#include <syscall.hpp>

namespace astia::proc::syscalls {

    struct umask_syscall : sys::syscall_handler<umask_syscall, sys::syscall_id::umask,
                                                sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor umask_descriptor = umask_syscall::descriptor;

} // namespace astia::proc::syscalls
