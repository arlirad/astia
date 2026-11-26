#include <astia/api/errno.h>

#include <syscall.hpp>

namespace astia::fs::syscalls {
    struct io_register : sys::syscall_handler<io_register, sys::syscall_id::io_register,
                                              sys::syscall_mode::sync> {
        static i64 invoke() {
            return -ENOSYS;
        }
    };
} // namespace astia::fs::syscalls
