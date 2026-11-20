#include <syscall.hpp>

namespace astia::proc::syscalls {

    struct shm_open_syscall
        : sys::syscall_handler<shm_open_syscall, sys::syscall_id::shm_open,
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor shm_open_descriptor =
        shm_open_syscall::descriptor;

} // namespace astia::proc::syscalls
