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

#include <syscall.hpp>
namespace astia::proc::syscalls {

    /// thread_join - Wait for and reap a user thread.
    ///
    /// Synopsis:
    ///     long thread_join(uint32_t tid, void **result);
    ///
    /// Description:
    ///     Waits for tid to terminate and optionally copies its result pointer
    ///     to result. Only one joiner is allowed.
    ///
    /// Return Value:
    ///     Returns zero after reaping the thread or a negative errno.
    ///
    /// Errors:
    ///     -EFAULT: The result pointer is not writable userspace memory.
    ///     -EINVAL: The thread is detached, already joined, or is the caller.
    ///     -ESRCH: The thread does not exist.
    struct thread_join : sys::syscall_handler<thread_join, sys::syscall_id::thread_join,
                                              sys::syscall_mode::sync> {
        static i64 invoke(sys::syscall_context& context) {
            u32   tid        = static_cast<u32>(context.arguments[0]);
            u64   result_ptr = static_cast<u64>(context.arguments[1]);
            auto* owner      = current_process();
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
            if (!output.null() &&
                !sys::copy_out(validated_output.value.bytes(), &result)) {
                return -EFAULT;
            }
            return 0;
        }
    };

    extern const sys::syscall_descriptor thread_join_descriptor = thread_join::descriptor;

} // namespace astia::proc::syscalls
