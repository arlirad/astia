#include "getcwd.hpp"

#include "../../proc.hpp"
#include "user_mem.hpp"
#include <astia/api/errno.h>
namespace astia::proc::syscall {
    i64 getcwd(u64 buffer, u64 length) {
        auto* owner = current_process();
        if (owner == nullptr)
            return -ESRCH;
        if (length == 0)
            return -ERANGE;
        char      copy[fs::path_max]{};
        const i64 rc = owner->copy_cwd(copy, length);
        if (rc < 0)
            return rc;
        const auto destination = sys::user_ptr<char>(buffer).validate(static_cast<usize>(rc));
        return destination && sys::copy_out(destination.value.bytes(static_cast<usize>(rc)), copy)
                   ? rc
                   : -EFAULT;
    }
}
