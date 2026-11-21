#include <astia/api/errno.h>

#include "../../proc/proc.hpp"
#include "../../sys/user_mem.hpp"
#include "../filesystem.hpp"

#include <syscall.hpp>

namespace astia::fs::syscalls {

    /// unmount - root-only teardown of a native VFS mount.
    ///
    /// The target is copied from userspace before authorization and lookup.
    /// The root mount, a mount with open descriptions, and a mount containing
    /// another mount return -EBUSY.  This only changes the in-memory namespace.
    struct unmount : sys::syscall_handler<unmount, sys::syscall_id::unmount,
                                          sys::syscall_mode::sync> {
        static i64 invoke(sys::syscall_context& context) {
            auto* current = proc::current_process();
            if (current == nullptr)
                return -ESRCH;
            if (current->euid() != 0)
                return -EPERM;
            char path[path_max]{};
            if (!sys::copy_in_string(
                    span<char>(path, sizeof path),
                    sys::user_ptr<char>(static_cast<u64>(context.arguments[0]))))
                return -EFAULT;
            return unmount_type(path);
        }
    };

    extern const sys::syscall_descriptor unmount_descriptor = unmount::descriptor;

} // namespace astia::fs::syscalls
