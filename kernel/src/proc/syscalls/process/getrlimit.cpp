#include <astia/api/errno.h>
#include <astia/api/syscalls.h>

#include "../../proc.hpp"
#include "user_mem.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {
    struct getrlimit : sys::syscall_handler<getrlimit, sys::syscall_id::getrlimit,
                                            sys::syscall_mode::sync> {
        static i64 invoke(i32 resource, sys::user_ptr<astia_rlimit> output) {
            auto* process = current_process();
            if (process == nullptr)
                return -ESRCH;

            astia_rlimit value{};

            const i64 rc = process->get_rlimit(resource, &value);
            if (rc < 0)
                return rc;

            const auto validated = output.validate();

            if (!validated || !sys::copy_out(validated.value.bytes(), &value))
                return -EFAULT;

            return 0;
        }
    };
} // namespace astia::proc::syscalls
