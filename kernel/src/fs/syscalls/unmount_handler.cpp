#include <syscall.hpp>

namespace astia::fs::syscalls {

    /// unmount - root-only teardown of a native VFS mount.
    ///
    /// The target is copied from userspace before authorization and lookup.
    /// The root mount, a mount with open descriptions, and a mount containing
    /// another mount return -EBUSY.  This only changes the in-memory namespace.
    struct unmount_syscall
        : sys::syscall_handler<unmount_syscall, sys::syscall_id::unmount,
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor unmount_descriptor = unmount_syscall::descriptor;

} // namespace astia::fs::syscalls
