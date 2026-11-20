#pragma once

#include <astia/common.hpp>

#include <syscall.hpp>

namespace astia::fs::syscall {
    /// mount - install a registered native filesystem at an existing directory.
    ///
    /// All names are copied before authorization.  The operation is root-only;
    /// it changes only the in-memory VFS namespace and never opens a host path.
    long mount(u64 type, u64 target, u64 source);
}

namespace astia::fs::syscall_handlers {

    struct mount_syscall : sys::syscall_handler<mount_syscall, sys::syscall_id::mount,
                                                sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

} // namespace astia::fs::syscall_handlers
