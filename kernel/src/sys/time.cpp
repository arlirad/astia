#include "syscall.hpp"
#include "user_mem.hpp"
#include <astia/api/errno.h>
#include <astia/api/syscalls.h>
#include <astia/arch/interrupts.hpp>

/*
 * SYS_CLOCK_GETTIME — the one clock read outside the rings.
 *
 * Every reading derives from arch::timer_ticks(), so the resolution is
 * one tick (100 Hz on x86_64 today): tv_nsec is exact for the tick and
 * advances in 10 ms steps. That is enough for libc's time(), timespec_get
 * and relative deadlines; a TSC/HPET source is what raises it later.
 *
 * CLOCK_MONOTONIC and CLOCK_BOOTTIME are uptime (astia has a single boot,
 * no suspend, so they agree). CLOCK_REALTIME is uptime anchored at the
 * constant below: there is no RTC driver yet, so wall-clock time starts
 * from an assumed instant instead of being read from hardware. Replace
 * the anchor with the CMOS/RTC reading the moment one exists — nothing
 * else in this file changes.
 */

namespace astia::sys {

    namespace {

        /// Assumed wall clock at boot: 2026-01-01T00:00:00Z. See above.
        constexpr i64 realtime_epoch_sec = 1767225600;

        i64 sys_clock_gettime(i64 clock_id, i64 out, i64, i64, i64, i64) {
            auto* ts_out = reinterpret_cast<void*>(static_cast<usize>(out));
            if (!sys::user_range_ok(ts_out, sizeof(astia_timespec))) {
                return -EFAULT;
            }
            const u64 hz = arch::timer_hz();
            if (hz == 0) {
                return -EIO; // no timer source: refuse instead of dividing by 0
            }

            const u64 ticks = arch::timer_ticks();
            i64       sec   = static_cast<i64>(ticks / hz);
            const i64 nsec = static_cast<i64>((ticks % hz) * 1000000000ull / hz); // < 1e9

            switch (clock_id) {
            case CLOCK_MONOTONIC:
            case CLOCK_BOOTTIME:
                break;
            case CLOCK_REALTIME:
                sec += realtime_epoch_sec;
                break;
            default:
                return -EINVAL;
            }

            astia_timespec ts{sec, nsec};
            if (!sys::copy_out(ts_out, &ts, sizeof(ts))) {
                return -EFAULT;
            }
            return 0;
        }

    } // namespace

    bool register_time_syscalls() {
        return register_syscall(syscall_id::clock_gettime, sys_clock_gettime);
    }

} // namespace astia::sys
