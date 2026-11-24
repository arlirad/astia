#include "../proc.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {

    struct setsid
        : sys::syscall_handler<setsid, sys::syscall_id::setsid, sys::syscall_mode::sync> {
        static i64 invoke() {
            return proc::setsid();
        }
    };

} // namespace astia::proc::syscalls
