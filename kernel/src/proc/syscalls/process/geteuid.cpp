#include <astia/api/errno.h>

#include "../../proc.hpp"
#include "user_mem.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {
    struct geteuid : sys::syscall_handler<geteuid, sys::syscall_id::geteuid,
                                          sys::syscall_mode::sync> {
        static i64 invoke() {
            auto* p = current_process();
            return p ? p->euid() : -ESRCH;
        }
    };

    extern const sys::syscall_descriptor geteuid_descriptor = geteuid::descriptor;

} // namespace astia::proc::syscalls
