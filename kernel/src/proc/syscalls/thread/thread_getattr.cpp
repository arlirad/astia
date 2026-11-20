#include "thread_getattr.hpp"

#include <astia/api/errno.h>
#include <astia/api/syscalls.h>

#include "../../../sys/user_mem.hpp"
#include "../../proc.hpp"
#include "../../process.hpp"

#include <syscall.hpp>

namespace astia::proc::syscall {

    i64 thread_getattr(u32 tid, u64 output) {
        auto* owner = current_process();
        if (owner == nullptr)
            return -ESRCH;

        const auto validated = sys::user_ptr<astia_thread_stack_info>(output).validate();
        if (!validated)
            return -EFAULT;

        process::thread_stack_snapshot snapshot{};
        auto                           target = owner->thread_stack_info(tid, snapshot);
        if (!target)
            return -ESRCH;
        if (!snapshot.supported)
            return -ENOTSUP;

        astia_thread_stack_info info{snapshot.stack_base,
                                     static_cast<u64>(snapshot.stack_size),
                                     snapshot.detached ? 1u : 0u, 0u};
        return sys::copy_out(validated.value.bytes(), &info) ? 0 : -EFAULT;
    }

}

namespace astia::proc::syscalls {

    /// ABI ID: thread_getattr.
    /// Sync/async mode: async.
    /// Arguments: six ABI register words are available through syscall_context.
    /// Result: a signed i64 result or a negative errno.
    /// Ownership: proc subsystem.
    /// Errors: invalid or unavailable operations return the owning implementation's
    /// negative errno.
    struct thread_getattr
        : sys::syscall_handler<thread_getattr, sys::syscall_id::thread_getattr,
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor thread_getattr_descriptor =
        thread_getattr::descriptor;

} // namespace astia::proc::syscalls
