#include <astia/api/errno.h>

#include "../../proc/proc.hpp"
#include "../../sys/user_mem.hpp"
#include "../filesystem.hpp"

#include <syscall.hpp>

namespace astia::fs::syscalls {
    struct unmount : sys::syscall_handler<unmount, sys::syscall_id::unmount,
                                          sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::user_ptr<char> target) {
            auto* current = proc::current_process();
            if (current == nullptr)
                co_return -ESRCH;

            if (current->euid() != 0)
                co_return -EPERM;

            char path[path_max]{};

            if (!sys::copy_in_string(span<char>(path, sizeof path), target))
                co_return -EFAULT;

            char      resolved[path_max]{};
            const i64 resolve_rc =
                current->resolve_path(path, resolved, sizeof resolved, false);
            if (resolve_rc < 0)
                co_return resolve_rc;

            co_return unmount_type(resolved);
        }
    };
} // namespace astia::fs::syscalls
