#include <syscall.hpp>

namespace astia::fs::syscalls {

    /// mount - install a registered native filesystem at an existing directory.
    ///
    /// All names are copied before authorization.  The operation is root-only;
    /// it changes only the in-memory VFS namespace and never opens a host path.
    struct mount_syscall : sys::syscall_handler<mount_syscall, sys::syscall_id::mount,
                                                sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor mount_descriptor = mount_syscall::descriptor;

} // namespace astia::fs::syscalls
