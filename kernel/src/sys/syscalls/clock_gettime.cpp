#include <astia/api/errno.h>
#include <astia/api/syscalls.h>
#include <astia/arch/interrupts.hpp>

#include "../time.hpp"
#include "user_mem.hpp"

#include <syscall.hpp>

namespace astia::sys::syscalls {
    struct clock_gettime
        : sys::syscall_handler<clock_gettime, sys::syscall_id::clock_gettime,
                               sys::syscall_mode::sync> {
        static i64 invoke(sys::syscall_context& context) {
            const i32  clock_id = static_cast<i32>(context.arguments[0]);
            const u64  out      = static_cast<u64>(context.arguments[1]);
            const auto ts_out   = sys::user_ptr<astia_timespec>(out).validate();
            if (!ts_out)
                return -EFAULT;
            const u64 hz = arch::timer_hz();
            if (hz == 0)
                return -EIO;
            const u64 ticks = arch::timer_ticks();
            i64       sec   = static_cast<i64>(ticks / hz);
            const i64 nsec = static_cast<i64>((ticks % hz) * nanoseconds_per_second / hz);
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
            const astia_timespec ts{sec, nsec};
            return sys::copy_out(ts_out.value.bytes(), &ts) ? 0 : -EFAULT;
        }
    };

    extern const sys::syscall_descriptor clock_gettime_descriptor =
        clock_gettime::descriptor;

} // namespace astia::sys::syscalls
