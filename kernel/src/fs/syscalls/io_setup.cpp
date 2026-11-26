#include <astia/api/aio.h>
#include <astia/api/errno.h>

#include "../../sys/aio.hpp"
#include "../../sys/user_mem.hpp"

#include <syscall.hpp>

namespace astia::fs::syscalls {
    struct io_setup : sys::syscall_handler<io_setup, sys::syscall_id::io_setup,
                                           sys::syscall_mode::sync> {
        static i64 invoke(u32 entries, u32 flags, sys::user_ptr<astia_rings*> out) {
            const auto ring_out = out.validate();
            if (!ring_out)
                return -EFAULT;

            return sys::io_setup(entries, flags, ring_out.value.get());
        }
    };
} // namespace astia::fs::syscalls
