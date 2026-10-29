#include "mount.hpp"
#include "../filesystem.hpp"
#include "../../proc/proc.hpp"
#include "../../sys/user_mem.hpp"
#include <astia/api/errno.h>

namespace astia::fs::syscall {
    long mount(u64 type_address, u64 target_address, u64 source_address) {
        char type[path_max]{};
        char target[path_max]{};
        char source[path_max]{};
        if (!sys::copy_in_string(type, sizeof type, sys::user_range::string(type_address)) ||
            !sys::copy_in_string(target, sizeof target, sys::user_range::string(target_address)))
            return -EFAULT;
        if (source_address != 0 &&
            !sys::copy_in_string(source, sizeof source, sys::user_range::string(source_address)))
            return -EFAULT;

        auto* current = proc::current_process();
        if (current == nullptr)
            return -ESRCH;
        if (current->euid() != 0)
            return -EPERM;
        return mount_type(type, target, source_address == 0 ? nullptr : source);
    }
}
