#include <astia/api/errno.h>
#include <astia/api/syscalls.h>

#include "../../proc.hpp"
#include "user_mem.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {
    struct setrlimit : sys::syscall_handler<setrlimit, sys::syscall_id::setrlimit,
                                            sys::syscall_mode::sync> {
        static i64 invoke(i32 resource, sys::user_ptr<const astia_rlimit> input) {
            auto* process = current_process();
            if (process == nullptr)
                return -ESRCH;

            astia_rlimit value{};
            const auto   validated = input.validate();

            if (!validated || !sys::copy_in(&value, validated.value.bytes()))
                return -EFAULT;

            return process->set_rlimit(resource, value);
        }
    };
} // namespace astia::proc::syscalls
