#include "../../io/aio/handler.hpp"
#include "common.hpp"

namespace astia::io::aio::operations {

    struct openat : handler<openat, opcode::openat> {
        static core::task<aio_result>
        invoke([[maybe_unused]] ring::ring_slot& slot, [[maybe_unused]] astia_rings* ring,
               astia_sqe sqe, [[maybe_unused]] core::cltoken token) noexcept {
            // The pathname is user memory: copy it into the kernel rather
            // than letting the VFS walk a raw user pointer.
            char path[256];
            if (!memory::copy_in_string(span<char>(path, sizeof path),
                                        memory::user_ptr<char>(sqe.address)))
                co_return -EFAULT;
            auto* current = proc::current_process();
            char  resolved[256];
            if (current == nullptr)
                co_return -EINVAL;
            long resolve_rc = resolve_at(current, sqe.fd == -1 ? AT_FDCWD : sqe.fd, path,
                                         resolved, sizeof resolved);
            if (resolve_rc >= 0)
                resolve_rc = fs::resolve_path(resolved, resolved, sizeof resolved, true);
            if (resolve_rc < 0)
                co_return (i32) resolve_rc;
            if (resolved[0] == '/' && resolved[1] == 'd' && resolved[2] == 'e' &&
                resolved[3] == 'v' && resolved[4] == '/' && resolved[5] == 't' &&
                resolved[6] == 't' && resolved[7] == 'y' && resolved[8] == '\0' &&
                !proc::has_controlling_terminal())
                co_return -ENXIO;
            const fs::file_mode existing_mode = fs::node_mode(resolved);
            if (existing_mode.exists() && existing_mode.is_directory() &&
                (sqe.op_flags & ASTIA_AIO_O_DIRECTORY) == 0)
                co_return -EISDIR;
            if (existing_mode.exists() && !existing_mode.is_directory() &&
                (sqe.op_flags & ASTIA_AIO_O_DIRECTORY) != 0)
                co_return -ENOTDIR;
            if (!existing_mode.exists() && (sqe.op_flags & ASTIA_AIO_O_CREAT) == 0)
                co_return -ENOENT;
            if (existing_mode.exists()) {
                auto*       cred  = proc::current_process();
                const uid_t owner = fs::node_uid(resolved);
                const gid_t group = fs::node_gid(resolved);
                if ((sqe.op_flags & ASTIA_AIO_O_RDWR) != 0 &&
                    !cred->can_access(existing_mode, owner, group,
                                      fs::permission_bits::write))
                    co_return -EACCES;
                if ((sqe.op_flags & ASTIA_AIO_O_WRONLY) == 0 &&
                    !cred->can_access(existing_mode, owner, group,
                                      fs::permission_bits::read))
                    co_return -EACCES;
            }
            mm::shptr<fs::open_file> object = existing_mode.is_directory()
                                                  ? fs::open_directory(resolved)
                                                  : fs::open(resolved, sqe.op_flags);
            if (!object)
                co_return -ENOENT;
            if ((sqe.op_flags & ASTIA_AIO_O_CREAT) != 0 && !existing_mode.exists()) {
                auto requested = mapper<fs::file_permissions>::from(
                    sqe.aux == 0 ? fs::default_file_permissions.raw() : sqe.aux);
                if (!requested.has_value)
                    co_return -EINVAL;
                (void) fs::chmod_path(resolved, requested.value);
                (void) fs::chown_path(resolved, uid_t{current->uid()},
                                      gid_t{current->gid()});
            }
            current = proc::current_process();
            if (current == nullptr)
                co_return -ESRCH;
            for (i32 i = 0; i < static_cast<i32>(proc::max_process_fds); ++i) {
                if (!current->descriptor(fd_t{i}) &&
                    current->set_descriptor(fd_t{i}, object,
                                            (sqe.op_flags & ASTIA_AIO_O_CLOEXEC) != 0
                                                ? static_cast<u32>(ASTIA_FD_CLOEXEC)
                                                : 0u))
                    co_return i;
            }
            co_return -EMFILE;
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor openat_descriptor = openat::descriptor;

} // namespace astia::io::aio::operations
