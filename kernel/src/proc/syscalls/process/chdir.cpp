#include <astia/api/errno.h>

#include "../../proc.hpp"
#include "user_mem.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {
    struct chdir
        : sys::syscall_handler<chdir, sys::syscall_id::chdir, sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::user_ptr<char> path) {
            auto* owner = current_process();
            if (owner == nullptr)
                co_return -ESRCH;

            char copy[fs::path_max]{};

            if (!sys::copy_in_string(span<char>(copy, sizeof copy), path))
                co_return -EFAULT;

            auto result = co_await owner->change_directory(copy);

            co_return result ? result.value : -error_errno(result.error);
        }
    };
} // namespace astia::proc::syscalls
