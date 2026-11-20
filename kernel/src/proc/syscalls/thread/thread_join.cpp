#include "thread_join.hpp"

#include <astia/api/aio.h>
#include <astia/api/errno.h>
#include <astia/api/syscalls.h>
#include <astia/arch/context.hpp>
#include <astia/arch/cpu.hpp>
#include <astia/fs.hpp>
#include <astia/log.hpp>
#include <astia/mm/heap.hpp>

#include "../../../ipc/futex.hpp"
#include "../../../ipc/mapping.hpp"
#include "../../mapping.hpp"
#include "../../proc.hpp"
#include "../../scheduler.hpp"
#include "syscall.hpp"
#include "user_mem.hpp"
namespace astia::proc::syscall {

    i64 thread_join(u32 tid, u64 result_ptr) {
        auto* owner = current_process();
        if (owner == nullptr) {
            return -ESRCH;
        }
        const sys::user_ptr<void*> output(result_ptr);
        const auto validated_output = output.null()
                                          ? optional<sys::validated_user_ptr<void*>>{}
                                          : output.validate();
        if (!output.null() && !validated_output) {
            return -EFAULT;
        }
        auto target = owner->thread_by_tid(tid);
        if (!target) {
            return -ESRCH;
        }
        void*      result = nullptr;
        const long rc     = target->join(output.null() ? nullptr : &result);
        if (rc < 0) {
            return rc;
        }
        if (!output.null() && !sys::copy_out(validated_output.value.bytes(), &result)) {
            return -EFAULT;
        }
        return 0;
    }
}

namespace astia::proc::syscalls {

    /// ABI ID: thread_join.
    /// Sync/async mode: async.
    /// Arguments: six ABI register words are available through syscall_context.
    /// Result: a signed i64 result or a negative errno.
    /// Ownership: proc subsystem.
    /// Errors: invalid or unavailable operations return the owning implementation's
    /// negative errno.
    struct thread_join : sys::syscall_handler<thread_join, sys::syscall_id::thread_join,
                                              sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor thread_join_descriptor = thread_join::descriptor;

} // namespace astia::proc::syscalls
