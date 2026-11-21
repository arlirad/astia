#include <astia/api/errno.h>
#include <astia/api/syscalls.h>
#include <astia/arch/interrupts.hpp>

#include "../time.hpp"
#include "user_mem.hpp"

#include <syscall.hpp>

namespace astia::sys::syscalls {
    struct clock_getres
        : sys::syscall_handler<clock_getres, sys::syscall_id::clock_getres,
                               sys::syscall_mode::sync> {
        static i64 invoke(i32 clock_id, u64 out) {
            if (clock_id != CLOCK_REALTIME && clock_id != CLOCK_MONOTONIC)
                return -EINVAL;
            const u32 hz = arch::timer_hz();
            if (hz == 0)
                return -EIO;
            if (out == 0)
                return 0;
            const auto ts_out = sys::user_ptr<astia_timespec>(out).validate();
            if (!ts_out)
                return -EFAULT;
            const u64            nsec = (nanoseconds_per_second + hz - 1) / hz;
            const astia_timespec resolution{0, static_cast<i64>(nsec)};
            return sys::copy_out(ts_out.value.bytes(), &resolution) ? 0 : -EFAULT;
        }
    };

    extern const sys::syscall_descriptor clock_getres_descriptor =
        clock_getres::descriptor;

} // namespace astia::sys::syscalls
