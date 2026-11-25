#include <astia/api/aio.h>
#include <astia/api/errno.h>
#include <astia/api/syscalls.h>
#include <astia/arch/context.hpp>
#include <astia/arch/cpu.hpp>
#include <astia/error.hpp>
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

    struct thread_join : sys::syscall_handler<thread_join, sys::syscall_id::thread_join,
                                              sys::syscall_mode::async> {
        static core::task<i64> invoke(u32 tid, sys::user_ptr<void*> output) {
            auto* owner = current_process();
            if (owner == nullptr) {
                co_return -ESRCH;
            }
            const auto validated_output = output.null()
                                              ? optional<sys::validated_user_ptr<void*>>{}
                                              : output.validate();
            if (!output.null() && !validated_output) {
                co_return -EFAULT;
            }
            auto target = owner->thread_by_tid(tid);
            if (!target) {
                co_return -ESRCH;
            }
            void* result = nullptr;
            auto  joined = co_await target->join(output.null() ? nullptr : &result);
            if (!joined)
                co_return error_errno(joined.error);
            const long rc = joined.value;
            if (rc < 0) {
                co_return rc;
            }
            if (!output.null() &&
                !sys::copy_out(validated_output.value.bytes(), &result)) {
                co_return -EFAULT;
            }
            co_return 0;
        }
    };

} // namespace astia::proc::syscalls
