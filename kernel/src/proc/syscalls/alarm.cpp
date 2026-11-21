#include <astia/api/errno.h>
#include <astia/arch/interrupts.hpp>

#include "../proc.hpp"

#include <syscall.hpp>

namespace astia::proc::syscall {

    i64 alarm(i64 seconds) {
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

} // namespace astia::proc::syscall

namespace astia::proc::syscalls {

    /// alarm - Schedule delivery of a process alarm.
    ///
    /// Synopsis:
    ///     long alarm(long seconds);
    ///
    /// Return Value:
    ///     Returns the previous alarm value or a negative errno.
    struct alarm
        : sys::syscall_handler<alarm, sys::syscall_id::alarm, sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::invoke_sync<&::astia::proc::syscall::alarm>(context);
        }
    };

    extern const sys::syscall_descriptor alarm_descriptor = alarm::descriptor;

} // namespace astia::proc::syscalls
