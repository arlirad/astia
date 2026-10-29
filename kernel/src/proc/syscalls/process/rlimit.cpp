#include "rlimit.hpp"
#include "../../proc.hpp"
#include "user_mem.hpp"
#include <astia/api/errno.h>
#include <astia/api/syscalls.h>

namespace astia::proc::syscall {
    i64 getrlimit(i32 resource, u64 output) {
        auto* process = current_process();
        if (process == nullptr) return -ESRCH;
        astia_rlimit value{};
        const i64 rc = process->get_rlimit(resource, &value);
        if (rc < 0) return rc;
        if (output == 0 || !sys::copy_out(sys::user_range::bytes(output, sizeof value), &value))
            return -EFAULT;
        return 0;
    }

    i64 setrlimit(i32 resource, u64 input) {
        auto* process = current_process();
        if (process == nullptr) return -ESRCH;
        astia_rlimit value{};
        if (input == 0 || !sys::copy_in(&value, sys::user_range::bytes(input, sizeof value)))
            return -EFAULT;
        return process->set_rlimit(resource, value);
    }
}
