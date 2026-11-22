#include "time.hpp"

#include <astia/arch/interrupts.hpp>

namespace astia::sys {

    /// Shared timer-source boundary retained after the clock syscall handlers
    /// moved to their canonical files under sys/syscalls.
    bool realtime_now(u64& sec, u64& nsec) {
        const u64 hz = arch::timer_hz();
        if (hz == 0)
            return false;
        const u64 ticks = arch::timer_ticks();
        sec  = static_cast<u64>(realtime_epoch_sec + static_cast<i64>(ticks / hz));
        nsec = (ticks % hz) * nanoseconds_per_second / hz;
        return true;
    }

} // namespace astia::sys
