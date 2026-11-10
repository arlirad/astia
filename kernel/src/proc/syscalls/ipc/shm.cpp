#include "shm.hpp"

#include "../../proc.hpp"
#include "user_mem.hpp"
#include <astia/api/aio.h>
#include <astia/api/errno.h>
#include <astia/api/syscalls.h>
#include <astia/fs.hpp>

namespace astia::proc::syscall {
    namespace {
        long copy_name(unsigned long address, char* name) {
            if (!sys::copy_in_string(span<char>(name, fs::path_max),
                                     sys::user_ptr<char>(address))) {
                const auto validated =
                    sys::user_span<const char>(address, fs::path_max).validate();
                return validated ? -ENAMETOOLONG : -EFAULT;
            }
            if (name[0] != '/' || name[1] == '\0')
                return -EINVAL;
            for (u32 i = 1; name[i] != '\0'; ++i)
                if (name[i] == '/')
                    return -EINVAL;
            return 0;
        }

        long native_flags(int flags, u32& wire) {
            const int access = flags & 3;
            if (access != ASTIA_SHM_O_RDONLY && access != ASTIA_SHM_O_WRONLY &&
                access != ASTIA_SHM_O_RDWR)
                return -EINVAL;
            if ((flags & ~(ASTIA_SHM_O_CREAT | ASTIA_SHM_O_EXCL | ASTIA_SHM_O_TRUNC | 3)) != 0)
                return -EINVAL;
            wire = access == ASTIA_SHM_O_RDWR
                       ? ASTIA_AIO_O_RDWR
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

    long umask(int mode) {
        auto* owner = current_process();
        if (owner == nullptr || mode < 0)
            return -ESRCH;
        return owner->set_umask(static_cast<u32>(mode));
    }

    long shm_open(unsigned long address, int flags, unsigned int mode) {
        auto* owner = current_process();
        if (owner == nullptr)
            return -ESRCH;
        char name[fs::path_max]{};
        long rc = copy_name(address, name);
        if (rc < 0)
            return rc;
        u32 wire = 0;
        rc = native_flags(flags, wire);
        if (rc < 0)
            return rc;
        const auto permissions = fs::file_permissions(
            static_cast<u16>(mode & fs::file_permissions_mask & ~owner->umask()));
        mm::shptr<fs::open_file> object;
        rc = fs::open_shared_memory(
            name, wire, fs::file_mode(fs::file_type::regular, permissions),
            uid_t{owner->uid()}, gid_t{owner->gid()}, uid_t{owner->euid()},
            gid_t{owner->egid()}, owner->euid() == 0, object);
        if (rc < 0)
            return rc;
        for (i32 fd = 0; fd < static_cast<i32>(max_process_fds); ++fd) {
            if (!owner->descriptor(fd_t{fd}) &&
                owner->set_descriptor(fd_t{fd}, object, ASTIA_FD_CLOEXEC))
                return fd;
        }
        return -EMFILE;
    }

    long shm_unlink(unsigned long address) {
        auto* owner = current_process();
        if (owner == nullptr)
            return -ESRCH;
        char name[fs::path_max]{};
        const long rc = copy_name(address, name);
        return rc < 0 ? rc : fs::unlink_shared_memory(name);
    }
}
