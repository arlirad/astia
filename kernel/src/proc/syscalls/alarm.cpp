#include <astia/api/errno.h>
#include <astia/arch/interrupts.hpp>

#include "../proc.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {

    struct alarm
        : sys::syscall_handler<alarm, sys::syscall_id::alarm, sys::syscall_mode::sync> {
        static i64 invoke(i64 seconds) {
            if (seconds < 0 || static_cast<u64>(seconds) > UINT32_MAX)
                return -EINVAL;
            auto* owner = current_process();
            if (owner == nullptr)
                return -ESRCH;
            const u32 hz = arch::timer_hz();
            if (hz == 0)
                return -EIO;
            return owner->alarm(static_cast<u32>(seconds), arch::timer_ticks(), hz);
        }
    };

} // namespace astia::proc::syscalls
