#include <astia/api/errno.h>
#include <astia/api/syscalls.h>

#include "../../proc.hpp"
#include "user_mem.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {
    struct process_snapshot
        : sys::syscall_handler<process_snapshot, sys::syscall_id::process_snapshot,
                               sys::syscall_mode::sync> {
        static i64 invoke(sys::user_ptr<astia_process_info> output, i64 capacity) {
            if (output.address() > static_cast<u64>(INT64_MAX) || capacity < 0 ||
                capacity > ASTIA_PROCESS_SNAPSHOT_MAX)
                return -EINVAL;

            return proc::process_snapshot(output.address(), static_cast<u32>(capacity));
        }
    };
} // namespace astia::proc::syscalls
