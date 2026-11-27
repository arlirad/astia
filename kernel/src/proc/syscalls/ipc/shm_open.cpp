#include <astia/api/aio.h>
#include <astia/api/errno.h>
#include <astia/api/syscalls.h>
#include <astia/fs.hpp>

#include "../../proc.hpp"
#include "shm_common.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {
    namespace {
        i64 native_flags(int flags, u32& wire) {
            const int access = flags & 3;

            if (access != ASTIA_SHM_O_RDONLY && access != ASTIA_SHM_O_WRONLY &&
                access != ASTIA_SHM_O_RDWR)
                return -EINVAL;

            if ((flags &
                 ~(ASTIA_SHM_O_CREAT | ASTIA_SHM_O_EXCL | ASTIA_SHM_O_TRUNC | 3)) != 0)
                return -EINVAL;

            wire = access == ASTIA_SHM_O_RDWR     ? ASTIA_AIO_O_RDWR
                   : access == ASTIA_SHM_O_WRONLY ? ASTIA_AIO_O_WRONLY
                                                  : ASTIA_AIO_O_RDONLY;

            if ((flags & ASTIA_SHM_O_CREAT) != 0)
                wire |= ASTIA_AIO_O_CREAT;

            if ((flags & ASTIA_SHM_O_EXCL) != 0)
                wire |= ASTIA_AIO_O_EXCL;

            if ((flags & ASTIA_SHM_O_TRUNC) != 0)
                wire |= ASTIA_AIO_O_TRUNC;

            return 0;
        }
    }

    struct shm_open : sys::syscall_handler<shm_open, sys::syscall_id::shm_open,
                                           sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::user_ptr<char> address, int flags,
                                      unsigned int mode) {
            auto* owner = current_process();
            if (owner == nullptr)
                co_return -ESRCH;

            char name[fs::path_max]{};
            i64  rc = detail::copy_shared_memory_name(address, name);
            if (rc < 0)
                co_return rc;

            u32 wire = 0;

            rc = native_flags(flags, wire);
            if (rc < 0)
                co_return rc;

            const auto permissions = fs::apply_umask(
                fs::file_permissions(static_cast<u16>(mode)), owner->umask());
            mm::shptr<fs::open_file> handle;

            rc = fs::open_shared_memory(
                name, wire, fs::file_mode(fs::file_type::regular, permissions),
                uid_t{owner->uid()}, gid_t{owner->gid()}, uid_t{owner->euid()},
                gid_t{owner->egid()}, owner->euid() == 0, handle);
            if (rc < 0)
                co_return rc;

            for (i32 fd = 0; fd < static_cast<i32>(max_process_fds); ++fd) {
                if (!owner->descriptor(fd_t{fd}) &&
                    owner->set_descriptor(fd_t{fd}, handle, ASTIA_FD_CLOEXEC))
                    co_return fd;
            }

            co_return -EMFILE;
        }
    };
} // namespace astia::proc::syscalls
