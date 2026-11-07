#include "thread_cancel_pending.hpp"

#include "../../proc.hpp"
#include <astia/api/errno.h>

namespace astia::proc::syscall {

    i64 thread_cancel_pending() {
        auto* self = current_thread();
        return self == nullptr ? -ESRCH : (self->cancellation_pending() ? 1 : 0);
    }

}
