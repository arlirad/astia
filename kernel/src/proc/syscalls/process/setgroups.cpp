#include "credentials.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {
    struct setgroups : sys::syscall_handler<setgroups, sys::syscall_id::setgroups,
                                            sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::invoke_sync<&::astia::proc::syscall::setgroups>(context);
        }
    };

    extern const sys::syscall_descriptor setgroups_descriptor = setgroups::descriptor;

} // namespace astia::proc::syscalls
