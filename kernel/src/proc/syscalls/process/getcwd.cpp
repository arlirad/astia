#include "getcwd.hpp"
#include "../../proc.hpp"
#include "user_mem.hpp"
#include <astia/api/errno.h>
namespace astia::proc::syscall {
    i64 getcwd(u64 buffer, u64 length) {
        auto* owner = current_process();
        if (owner == nullptr) return -ESRCH;
        if (length == 0 || length > fs::path_max) return -ERANGE;
        const auto range = sys::user_range::bytes(buffer, length);
        if (!sys::user_range_ok(range)) return -EFAULT;
        char copy[fs::path_max]{};
        const i64 rc = owner->copy_cwd(copy, length);
        if (rc < 0) return rc;
        return sys::copy_out(range, copy) ? rc : -EFAULT;
    }
}
