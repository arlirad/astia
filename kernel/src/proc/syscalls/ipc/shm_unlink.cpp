#include "shm.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {
    struct shm_unlink : sys::syscall_handler<shm_unlink, sys::syscall_id::shm_unlink,
                                             sys::syscall_mode::sync> {
        static i64 invoke(sys::syscall_context& context) {
            return sys::invoke_sync<&::astia::proc::syscall::shm_unlink>(context);
        }
    };

    extern const sys::syscall_descriptor shm_unlink_descriptor = shm_unlink::descriptor;

} // namespace astia::proc::syscalls
