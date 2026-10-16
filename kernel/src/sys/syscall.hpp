#pragma once

#include <astia/common.hpp>

namespace astia::sys {

    /// Architecture-independent syscall numbers mirror the public ABI.
    enum class syscall_id : u32 {
        io_setup = 0,
        io_destroy,
        io_enter,
        io_register,
        mmap,
        munmap,
        mprotect,
        clock_gettime,
        exit,
        yield,
        getpid,
        gettid,
        futex,
        waitpid,
        spawnve,
        fork,
        thread_create,
        thread_exit,
        thread_join,
        thread_detach,
    };

    /// Full syscall ABI handler signature: six arguments after the id.
    using syscall_handler = i64 (*)(i64 arg0, i64 arg1, i64 arg2, i64 arg3, i64 arg4,
                                    i64 arg5);

    /// Register a system call handler
    bool register_syscall(syscall_id id, syscall_handler handler);

    /// Dispatch a system call
    i64 dispatch(syscall_id id, i64 a0, i64 a1, i64 a2, i64 a3, i64 a4, i64 a5);

} // namespace astia::sys
