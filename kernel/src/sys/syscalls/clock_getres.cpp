#include <astia/api/errno.h>
#include <astia/api/syscalls.h>
#include <astia/arch/interrupts.hpp>
#include <astia/memory.hpp>

#include "../time.hpp"
#include "user_mem.hpp"

#include <syscall.hpp>

namespace astia::sys::syscalls {
    struct clock_getres
        : sys::syscall_handler<clock_getres, sys::syscall_id::clock_getres,
                               sys::syscall_mode::sync> {
        static i64 invoke(i32 clock_id, sys::user_ptr<astia_timespec> out) {
            if (clock_id != CLOCK_REALTIME && clock_id != CLOCK_MONOTONIC)
                return -EINVAL;
            const u32 hz = arch::timer_hz();
            if (hz == 0)
                return -EIO;
            if (out.null())
                return 0;
            const auto ts_out = out.validate();
            if (!ts_out)
                return -EFAULT;
            const u64      nsec = (nanoseconds_per_second + hz - 1) / hz;
            astia_timespec resolution;
            fill_zero(&resolution, sizeof resolution);
            resolution.tv_sec  = 0;
            resolution.tv_nsec = static_cast<i64>(nsec);
            return sys::copy_out(ts_out.value.bytes(), &resolution) ? 0 : -EFAULT;
        }
    };

    extern const sys::syscall_descriptor clock_getres_descriptor =
        clock_getres::descriptor;

} // namespace astia::sys::syscalls
