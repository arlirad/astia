#include <syscall.hpp>

namespace astia::proc::syscalls {

    struct mprotect_syscall
        : sys::syscall_handler<mprotect_syscall, sys::syscall_id::mprotect,
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor mprotect_descriptor =
        mprotect_syscall::descriptor;

} // namespace astia::proc::syscalls
