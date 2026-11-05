#include "getpgid.hpp"

#include "../proc.hpp"
#include <astia/api/errno.h>

namespace astia::proc::syscall {
    i64 getpgid(i64 raw_target) {
        if (raw_target < 0 || raw_target > 0xffffffffll)
            return -ESRCH;
        return proc::getpgid(pid_from_raw(static_cast<u32>(raw_target)));
    }
}
