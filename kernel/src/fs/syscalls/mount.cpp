#include <astia/api/errno.h>

#include "../../proc/proc.hpp"
#include "../../sys/user_mem.hpp"
#include "../filesystem.hpp"

#include <syscall.hpp>

namespace astia::fs::syscalls {

    /// mount - install a registered native filesystem at an existing directory.
    ///
    /// All names are copied before authorization.  The operation is root-only;
    /// it changes only the in-memory VFS namespace and never opens a host path.
    struct mount
        : sys::syscall_handler<mount, sys::syscall_id::mount, sys::syscall_mode::sync> {
        static i64 invoke(u64 type_address, u64 target_address, u64 source_address) {
            char type[path_max]{};
            char target[path_max]{};
            char source[path_max]{};
            if (!sys::copy_in_string(span<char>(type, sizeof type),
                                     sys::user_ptr<char>(type_address)) ||
                !sys::copy_in_string(span<char>(target, sizeof target),
                                     sys::user_ptr<char>(target_address)))
                return -EFAULT;
            if (source_address != 0 &&
                !sys::copy_in_string(span<char>(source, sizeof source),
                                     sys::user_ptr<char>(source_address)))
                return -EFAULT;

            auto* current = proc::current_process();
            if (current == nullptr)
                return -ESRCH;
            if (current->euid() != 0)
                return -EPERM;
            return mount_type(type, target, source_address == 0 ? nullptr : source);
        }
    };

    extern const sys::syscall_descriptor mount_descriptor = mount::descriptor;

} // namespace astia::fs::syscalls
