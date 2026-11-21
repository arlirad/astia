#include "shm.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {
    struct shm_open : sys::syscall_handler<shm_open, sys::syscall_id::shm_open,
                                           sys::syscall_mode::sync> {
        static i64 invoke(sys::syscall_context& context) {
            return sys::invoke_sync<&::astia::proc::syscall::shm_open>(context);
        }
    };

    extern const sys::syscall_descriptor shm_open_descriptor = shm_open::descriptor;

} // namespace astia::proc::syscalls
