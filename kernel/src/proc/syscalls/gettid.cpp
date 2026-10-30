#include "gettid.hpp"

#include "../proc.hpp"
#include <astia/api/errno.h>
namespace astia::proc::syscall {
    i64 gettid() {
        auto* thread = current_thread();
        return thread == nullptr ? -ESRCH : static_cast<i64>(thread->tid());
    }
}
