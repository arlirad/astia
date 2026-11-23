#include <astia/api/errno.h>
#include <astia/fs.hpp>

#include "../../proc.hpp"
#include "shm_common.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {
    struct shm_unlink : sys::syscall_handler<shm_unlink, sys::syscall_id::shm_unlink,
                                             sys::syscall_mode::sync> {
        static i64 invoke(sys::user_ptr<char> address) {
            auto* owner = current_process();
            if (owner == nullptr)
                return -ESRCH;
            char      name[fs::path_max]{};
            const i64 rc = detail::copy_shared_memory_name(address, name);
            return rc < 0 ? rc : fs::unlink_shared_memory(name);
        }
    };

} // namespace astia::proc::syscalls
