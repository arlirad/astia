#include <astia/api/aio.h>

#include "../../sys/aio.hpp"

#include <syscall.hpp>

namespace astia::fs::syscalls {

    struct io_submit : sys::syscall_handler<io_submit, sys::syscall_id::io_submit,
                                            sys::syscall_mode::sync> {
        static i64 invoke(u64 ring, u32 submit) {
            return sys::io_submit(
                reinterpret_cast<astia_rings*>(static_cast<usize>(ring)), submit);
        }
    };

} // namespace astia::fs::syscalls
