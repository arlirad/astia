#include <syscall.hpp>

namespace astia::proc::syscalls {

    struct setregid_syscall
        : sys::syscall_handler<setregid_syscall, sys::syscall_id::setregid,
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor setregid_descriptor =
        setregid_syscall::descriptor;

} // namespace astia::proc::syscalls
