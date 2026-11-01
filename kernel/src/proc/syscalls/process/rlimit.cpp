#include "rlimit.hpp"

#include "../../proc.hpp"
#include "user_mem.hpp"
#include <astia/api/errno.h>
#include <astia/api/syscalls.h>

namespace astia::proc::syscall {
    i64 getrlimit(i32 resource, u64 output) {
        auto* process = current_process();
        if (process == nullptr)
            return -ESRCH;
        astia_rlimit value{};
        const i64    rc = process->get_rlimit(resource, &value);
        if (rc < 0)
            return rc;
        const sys::user_ptr<astia_rlimit> destination(output);
        if (destination.null() || !sys::copy_out(destination.bytes(), &value))
            return -EFAULT;
        return 0;
    }

    i64 setrlimit(i32 resource, u64 input) {
        auto* process = current_process();
        if (process == nullptr)
            return -ESRCH;
        astia_rlimit value{};
        const sys::user_ptr<const astia_rlimit> source(input);
        if (source.null() || !sys::copy_in(&value, source.bytes()))
            return -EFAULT;
        return process->set_rlimit(resource, value);
    }
}
