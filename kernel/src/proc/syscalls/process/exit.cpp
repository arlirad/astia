#include "exit.hpp"

#include "../../proc.hpp"
#include <astia/api/errno.h>
#include <astia/log.hpp>
namespace astia::proc::syscall {
    i64 exit(i64 status) {
        auto* thread = current_thread();
        if (thread == nullptr)
            return -ESRCH;
        astia::log::context<"proc">::info(
            "user thread exit pid=%d tid=%d status=%ld",
            thread->owner() ? static_cast<int>(static_cast<u32>(thread->owner()->pid()))
                            : -1,
            static_cast<int>(thread->tid()), status);
        thread->exit(reinterpret_cast<void*>(static_cast<usize>(status)));
        __builtin_unreachable();
    }
}
