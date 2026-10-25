#include "chdir.hpp"
#include "../../proc.hpp"
#include "user_mem.hpp"
#include <astia/api/errno.h>
namespace astia::proc::syscall {
    i64 chdir(u64 path) {
        auto* owner = current_process();
        if (owner == nullptr) return -ESRCH;
        char copy[fs::path_max]{};
        if (!sys::copy_in_string(copy, sizeof copy, sys::user_range::string(path)))
            return -EFAULT;
        return owner->change_directory(copy);
    }
}
