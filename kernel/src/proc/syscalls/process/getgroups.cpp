#include <astia/api/errno.h>

#include "../../proc.hpp"
#include "user_mem.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {
    struct getgroups : sys::syscall_handler<getgroups, sys::syscall_id::getgroups,
                                            sys::syscall_mode::sync> {
        static i64 invoke(i64 n, sys::user_ptr<u32> out) {
            auto* p = current_process();
            if (!p)
                return -ESRCH;
            if (n < 0 || n > static_cast<i64>(max_supplementary_groups))
                return -EINVAL;
            u32 values[max_supplementary_groups]{};
            i64 rc = p->getgroups(static_cast<i32>(n), values);
            if (rc < 0 || n == 0)
                return rc;
            const auto destination = out.validate(static_cast<usize>(rc));
            if (!destination ||
                !sys::copy_out(destination.value.bytes(static_cast<usize>(rc)), values))
                return -EFAULT;
            return rc;
        }
    };

    extern const sys::syscall_descriptor getgroups_descriptor = getgroups::descriptor;

} // namespace astia::proc::syscalls
