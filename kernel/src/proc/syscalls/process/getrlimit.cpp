#include <astia/api/errno.h>
#include <astia/api/syscalls.h>

#include "../../proc.hpp"
#include "user_mem.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {
    struct getrlimit : sys::syscall_handler<getrlimit, sys::syscall_id::getrlimit,
                                            sys::syscall_mode::sync> {
        static i64 invoke(sys::syscall_context& context) {
            i32   resource = static_cast<i32>(context.arguments[0]);
            u64   output   = static_cast<u64>(context.arguments[1]);
            auto* process  = current_process();
            if (process == nullptr)
                return -ESRCH;
            astia_rlimit value{};
            const i64    rc = process->get_rlimit(resource, &value);
            if (rc < 0)
                return rc;
            const sys::user_ptr<astia_rlimit> destination(output);
            const auto validated = sys::user_span<astia_rlimit>(output, 1).validate();
            if (!validated || !sys::copy_out(validated.value.bytes(), &value))
                return -EFAULT;
            return 0;
        }
    };

    extern const sys::syscall_descriptor getrlimit_descriptor = getrlimit::descriptor;

} // namespace astia::proc::syscalls
