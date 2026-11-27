#include <astia/api/aio.h>

#include "../../sys/aio.hpp"

#include <syscall.hpp>

namespace astia::fs::syscalls {
    struct io_destroy : sys::syscall_handler<io_destroy, sys::syscall_id::io_destroy,
                                             sys::syscall_mode::sync> {
        static i64 invoke(u64 ring) {
            return sys::io_destroy(
                reinterpret_cast<astia_rings*>(static_cast<usz>(ring)));
        }
    };
} // namespace astia::fs::syscalls
