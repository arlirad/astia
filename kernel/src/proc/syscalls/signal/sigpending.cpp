#include "sigpending.hpp"

#include <astia/api/errno.h>

#include "../../proc.hpp"
#include "user_mem.hpp"

#include <syscall.hpp>

namespace astia::proc::syscall {

    i64 sigpending(u64 set_ptr) {
        auto*      owner = current_process();
        auto*      self  = current_thread();
        const auto range = sys::user_ptr<u32>(set_ptr).validate();
        if (owner == nullptr || self == nullptr)
            return -ESRCH;
        if (!range)
            return -EFAULT;
        const signal_mask blocked = self->signals().blocked();
        const signal_mask pending =
            (self->signals().pending() | owner->signals().pending()) & blocked;
        const u32 value = pending;
        return sys::copy_out(range.value.bytes(), &value) ? 0 : -EFAULT;
    }

}

namespace astia::proc::syscalls {

    /// ABI ID: sigpending.
    /// Sync/async mode: async.
    /// Arguments: six ABI register words are available through syscall_context.
    /// Result: a signed i64 result or a negative errno.
    /// Ownership: proc subsystem.
    /// Errors: invalid or unavailable operations return the owning implementation's
    /// negative errno.
    struct sigpending : sys::syscall_handler<sigpending, sys::syscall_id::sigpending,
                                             sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor sigpending_descriptor = sigpending::descriptor;

} // namespace astia::proc::syscalls
