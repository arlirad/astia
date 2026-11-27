#include <astia/api/errno.h>

#include "../../proc.hpp"
#include "user_mem.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {
    struct setgroups : sys::syscall_handler<setgroups, sys::syscall_id::setgroups,
                                            sys::syscall_mode::sync> {
        static i64 invoke(i64 n, sys::user_ptr<const u32> in) {
            auto* p = current_process();
            if (!p)
                return -ESRCH;

            if (n < 0 || n > static_cast<i64>(max_supplementary_groups))
                return -EINVAL;

            u32 values[max_supplementary_groups]{};

            const auto source = in.validate(static_cast<usz>(n));

            if (n != 0 && (!source || !sys::copy_in(values, source.value.bytes(
                                                                static_cast<usz>(n)))))
                return -EFAULT;

            return p->setgroups(static_cast<i32>(n), values);
        }
    };
} // namespace astia::proc::syscalls
