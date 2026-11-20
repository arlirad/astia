#include <syscall.hpp>

namespace astia::proc::syscalls {

    struct futex_syscall : sys::syscall_handler<futex_syscall, sys::syscall_id::futex,
                                                sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor futex_descriptor = futex_syscall::descriptor;

} // namespace astia::proc::syscalls
