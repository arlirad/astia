#include "getcwd.hpp"

#include <astia/api/errno.h>

#include "../../proc.hpp"
#include "user_mem.hpp"

#include <syscall.hpp>
namespace astia::proc::syscall {
    i64 getcwd(u64 buffer, u64 length) {
        auto* owner = current_process();
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
        return destination && sys::copy_out(
                                  destination.value.bytes(static_cast<usize>(rc)), copy)
                   ? rc
                   : -EFAULT;
    }
}

namespace astia::proc::syscalls {

    /// ABI ID: getcwd.
    /// Sync/async mode: async.
    /// Arguments: six ABI register words are available through syscall_context.
    /// Result: a signed i64 result or a negative errno.
    /// Ownership: proc subsystem.
    /// Errors: invalid or unavailable operations return the owning implementation's
    /// negative errno.
    struct getcwd : sys::syscall_handler<getcwd, sys::syscall_id::getcwd,
                                         sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor getcwd_descriptor = getcwd::descriptor;

} // namespace astia::proc::syscalls
