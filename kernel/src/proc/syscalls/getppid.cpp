#include "getppid.hpp"

#include "../proc.hpp"
#include <astia/api/errno.h>

namespace astia::proc::syscall {
    i64 getppid() {
        auto* process = current_process();
        return process == nullptr ? -ESRCH
                                 : static_cast<i64>(raw_id(process->parent_pid()));
    }
}
