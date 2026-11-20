#include <syscall.hpp>

namespace astia::proc::syscalls {

    struct setgroups_syscall
        : sys::syscall_handler<setgroups_syscall, sys::syscall_id::setgroups,
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor setgroups_descriptor =
        setgroups_syscall::descriptor;

} // namespace astia::proc::syscalls
