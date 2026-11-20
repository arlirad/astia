#include <syscall.hpp>

namespace astia::proc::syscalls {

    struct fchdir_syscall : sys::syscall_handler<fchdir_syscall, sys::syscall_id::fchdir,
                                                 sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor fchdir_descriptor = fchdir_syscall::descriptor;

} // namespace astia::proc::syscalls
