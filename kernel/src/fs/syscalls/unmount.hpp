#pragma once

#include <astia/common.hpp>

#include <syscall.hpp>

namespace astia::fs::syscall {
    /// unmount - root-only teardown of a native VFS mount.
    ///
    /// The target is copied from userspace before authorization and lookup.
    /// The root mount, a mount with open descriptions, and a mount containing
    /// another mount return -EBUSY.  This only changes the in-memory namespace.
    long unmount(u64 target);
}

namespace astia::fs::syscall_handlers {

    struct unmount_syscall
        : sys::syscall_handler<unmount_syscall, sys::syscall_id::unmount,
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

} // namespace astia::fs::syscall_handlers
