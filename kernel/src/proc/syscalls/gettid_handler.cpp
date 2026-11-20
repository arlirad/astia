#include <syscall.hpp>

namespace astia::proc::syscalls {

    /// gettid - return the calling thread ID.
    ///
    /// Synopsis:
    ///     long gettid(void);
    struct gettid_syscall : sys::syscall_handler<gettid_syscall, sys::syscall_id::gettid,
                                                 sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor gettid_descriptor = gettid_syscall::descriptor;

} // namespace astia::proc::syscalls
