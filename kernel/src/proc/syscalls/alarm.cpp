#include "alarm.hpp"

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
    /// ABI ID: alarm.
    /// Sync/async mode: async.
    /// Arguments: six ABI register words are available through syscall_context.
    /// Result: a signed i64 result or a negative errno.
    /// Ownership: proc subsystem.
    /// Errors: invalid or unavailable operations return the owning implementation's
    /// negative errno.
    struct alarm
        : sys::syscall_handler<alarm, sys::syscall_id::alarm, sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor alarm_descriptor = alarm::descriptor;

} // namespace astia::proc::syscalls
