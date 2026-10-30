#include "thread_join.hpp"

#include "../../../ipc/futex.hpp"
#include "../../../ipc/mapping.hpp"
#include "../../mapping.hpp"
#include "../../proc.hpp"
#include "../../scheduler.hpp"
#include "syscall.hpp"
#include "user_mem.hpp"
#include <astia/api/aio.h>
#include <astia/api/errno.h>
#include <astia/api/syscalls.h>
#include <astia/arch/context.hpp>
#include <astia/arch/cpu.hpp>
#include <astia/fs.hpp>
#include <astia/log.hpp>
#include <astia/mm/heap.hpp>
namespace astia::proc::syscall {

    i64 thread_join(u32 tid, u64 result_ptr) {
        auto* owner = current_process();
        if (owner == nullptr) {
            return -ESRCH;
        }
        const sys::user_range output = sys::user_range::bytes(result_ptr, sizeof(void*));
        if (!output.null() && !sys::user_range_ok(output)) {
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
        if (!output.null() && !sys::copy_out(output, &result)) {
            return -EFAULT;
        }
        return 0;
    }
}
