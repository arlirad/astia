#include "ipc/shm.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {
    struct umask
        : sys::syscall_handler<umask, sys::syscall_id::umask, sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::invoke_sync<&::astia::proc::syscall::umask>(context);
        }
    };

    extern const sys::syscall_descriptor umask_descriptor = umask::descriptor;

} // namespace astia::proc::syscalls
