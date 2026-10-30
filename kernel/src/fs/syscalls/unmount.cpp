#include "unmount.hpp"

#include "../../proc/proc.hpp"
#include "../../sys/user_mem.hpp"
#include "../filesystem.hpp"
#include <astia/api/errno.h>

namespace astia::fs::syscall {
    long unmount(u64 target_address) {
        auto* current = proc::current_process();
        if (current == nullptr)
            return -ESRCH;
        if (current->euid() != 0)
            return -EPERM;
        char path[path_max]{};
        if (!sys::copy_in_string(path, sizeof path,
                                 sys::user_range::string(target_address)))
            return -EFAULT;
        return unmount_type(path);
    }
}
