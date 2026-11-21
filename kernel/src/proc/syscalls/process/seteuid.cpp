#include "credentials.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {
    struct seteuid : sys::syscall_handler<seteuid, sys::syscall_id::seteuid,
                                          sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::invoke_sync<&::astia::proc::syscall::seteuid>(context);
        }
    };

    extern const sys::syscall_descriptor seteuid_descriptor = seteuid::descriptor;

} // namespace astia::proc::syscalls
