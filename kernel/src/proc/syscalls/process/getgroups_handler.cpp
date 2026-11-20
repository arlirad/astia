#include <syscall.hpp>

namespace astia::proc::syscalls {

    struct getgroups_syscall
        : sys::syscall_handler<getgroups_syscall, sys::syscall_id::getgroups,
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor getgroups_descriptor =
        getgroups_syscall::descriptor;

} // namespace astia::proc::syscalls
