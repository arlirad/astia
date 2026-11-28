#include <astia/api/errno.h>
#include <astia/fs.hpp>
#include <astia/mapping.hpp>

#include "../../exec_permission.hpp"
#include "../../proc.hpp"
#include "argv.hpp"
#include "path_staging.hpp"
#include "syscall.hpp"
#include "user_mem.hpp"

namespace astia::proc::syscalls {
    struct spawnve : sys::syscall_handler<spawnve, sys::syscall_id::spawnve,
                                          sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::user_ptr<char>      path_ptr,
                                      sys::user_ptr<const u64> argv_ptr,
                                      sys::user_ptr<const u64> envp_ptr) {
            auto* owner = current_process();

            if (owner == nullptr || current_thread() == nullptr)
                co_return -ESRCH;

            detail::path_staging path{path_ptr};

            const i64 staged = path.copy();
            if (staged != 0)
                co_return staged;

            detail::argument_staging staging;

            if (!staging.allocate())
                co_return -ENOMEM;

            const i64 copied = staging.copy(argv_ptr.address(), envp_ptr.address());
            if (copied != 0)
                co_return copied;

            char      resolved[fs::path_max]{};
            const i64 resolve_rc =
                owner->resolve_path(path.c_str(), resolved, sizeof resolved, true);
            if (resolve_rc < 0)
                co_return resolve_rc;

            const i64 permission_rc = exec_path_check(*owner, resolved);
            if (permission_rc < 0)
                co_return permission_rc;

            auto file = fs::open_path(resolved);
            if (!file)
                co_return -ENOENT;

            const long image_size = file->size();
            if (image_size <= 0)
                co_return -EINVAL;

            auto child = co_await astia::proc::spawnve(
                owner, static_cast<mm::shptr<fs::open_file>&&>(file),
                static_cast<usz>(image_size), staging.argv(), staging.envp());
            if (!child)
                co_return -astia::errno_value(child.error);

            co_return static_cast<i64>(raw_id(child.value->pid()));
        }
    };
} // namespace astia::proc::syscalls
