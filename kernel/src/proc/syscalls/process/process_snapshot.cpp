#include <astia/api/errno.h>
#include <astia/api/syscalls.h>

#include "../../proc.hpp"

#include <syscall.hpp>

namespace astia::proc::syscall {
    i64 process_snapshot(i64 output, i64 capacity) {
        if (output < 0 || capacity < 0 || capacity > ASTIA_PROCESS_SNAPSHOT_MAX)
            return -EINVAL;
        return proc::process_snapshot(static_cast<u64>(output),
                                      static_cast<u32>(capacity));
    }
}

namespace astia::proc::syscalls {
    struct process_snapshot
        : sys::syscall_handler<process_snapshot, sys::syscall_id::process_snapshot,
                               sys::syscall_mode::sync> {
        static i64 invoke(sys::syscall_context& context) {
            return sys::invoke_sync<&::astia::proc::syscall::process_snapshot>(context);
        }
    };

    extern const sys::syscall_descriptor process_snapshot_descriptor =
        process_snapshot::descriptor;

} // namespace astia::proc::syscalls
