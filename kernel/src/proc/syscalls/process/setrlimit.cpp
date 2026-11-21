#include <astia/api/errno.h>
#include <astia/api/syscalls.h>

#include "../../proc.hpp"
#include "user_mem.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {
    struct setrlimit : sys::syscall_handler<setrlimit, sys::syscall_id::setrlimit,
                                            sys::syscall_mode::sync> {
        static i64 invoke(sys::syscall_context& context) {
            i32   resource = static_cast<i32>(context.arguments[0]);
            u64   input    = static_cast<u64>(context.arguments[1]);
            auto* process  = current_process();
            if (process == nullptr)
                return -ESRCH;
            astia_rlimit                            value{};
            const sys::user_ptr<const astia_rlimit> source(input);
            const auto                              validated =
                sys::user_span<const astia_rlimit>(input, 1).validate();
            if (!validated || !sys::copy_in(&value, validated.value.bytes()))
                return -EFAULT;
            return process->set_rlimit(resource, value);
        }
    };

    extern const sys::syscall_descriptor setrlimit_descriptor = setrlimit::descriptor;

} // namespace astia::proc::syscalls
