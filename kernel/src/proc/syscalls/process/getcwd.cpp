#include <astia/api/errno.h>

#include "../../proc.hpp"
#include "user_mem.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {

    /// getcwd - copy the caller's working directory.
    ///
    /// Synopsis:
    ///     long getcwd(char *buffer, size_t length);
    ///
    /// Errors:
    ///     -EFAULT: The output buffer is inaccessible.
    ///     -ERANGE: The buffer is too small.
    ///     -ESRCH: There is no current process.
    struct getcwd
        : sys::syscall_handler<getcwd, sys::syscall_id::getcwd, sys::syscall_mode::sync> {
        static i64 invoke(sys::syscall_context& context) {
            u64   buffer = static_cast<u64>(context.arguments[0]);
            u64   length = static_cast<u64>(context.arguments[1]);
            auto* owner  = current_process();
            if (owner == nullptr)
                return -ESRCH;
            if (length == 0)
                return -ERANGE;
            char      copy[fs::path_max]{};
            const i64 rc = owner->copy_cwd(copy, length);
            if (rc < 0)
                return rc;
            const auto destination =
                sys::user_ptr<char>(buffer).validate(static_cast<usize>(rc));
            return destination &&
                           sys::copy_out(destination.value.bytes(static_cast<usize>(rc)),
                                         copy)
                       ? rc
                       : -EFAULT;
        }
    };

    extern const sys::syscall_descriptor getcwd_descriptor = getcwd::descriptor;

} // namespace astia::proc::syscalls
