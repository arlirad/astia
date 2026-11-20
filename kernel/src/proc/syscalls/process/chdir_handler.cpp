#include <syscall.hpp>

namespace astia::proc::syscalls {

    struct chdir_syscall : sys::syscall_handler<chdir_syscall, sys::syscall_id::chdir,
                                                sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor chdir_descriptor = chdir_syscall::descriptor;

} // namespace astia::proc::syscalls
