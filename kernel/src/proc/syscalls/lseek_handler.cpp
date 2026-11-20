#include <syscall.hpp>

namespace astia::proc::syscalls {

    struct lseek_syscall : sys::syscall_handler<lseek_syscall, sys::syscall_id::lseek,
                                                sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor lseek_descriptor = lseek_syscall::descriptor;

} // namespace astia::proc::syscalls
