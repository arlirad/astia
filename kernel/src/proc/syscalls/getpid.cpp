#include "getpid.hpp"
#include "../proc.hpp"
#include <astia/api/errno.h>
namespace astia::proc::syscall {
    i64 getpid() {
        auto* process = current_process();
        return process == nullptr ? -ESRCH : static_cast<i64>(process->pid());
    }
}
