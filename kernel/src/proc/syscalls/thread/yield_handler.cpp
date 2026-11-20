#include <syscall.hpp>

namespace astia::proc::syscalls {

    struct yield_syscall : sys::syscall_handler<yield_syscall, sys::syscall_id::yield,
                                                sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor yield_descriptor = yield_syscall::descriptor;

} // namespace astia::proc::syscalls
