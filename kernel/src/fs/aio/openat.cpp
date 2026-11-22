#include "../../io/aio/handler.hpp"
#include "common.hpp"

namespace astia::io::aio::operations {

    namespace {

        bool handle_openat(astia_rings* ring, const astia_sqe& s) {
            // The pathname is user memory: copy it into the kernel rather
            // than letting the VFS walk a raw user pointer.
            char path[256];
            if (!memory::copy_in_string(span<char>(path, sizeof path),
                                        memory::user_ptr<char>(s.address)))
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            auto* current = proc::current_process();
            char  resolved[256];
            if (current == nullptr)
                return cq_post_guarded(ring, s.user_data, -EINVAL);
            long resolve_rc = resolve_at(current, s.fd == -1 ? AT_FDCWD : s.fd, path,
                                         resolved, sizeof resolved);
            if (resolve_rc >= 0)
                resolve_rc = fs::resolve_path(resolved, resolved, sizeof resolved, true);
            if (resolve_rc < 0)
                return cq_post_guarded(ring, s.user_data, (i32) resolve_rc);
            if (resolved[0] == '/' && resolved[1] == 'd' && resolved[2] == 'e' &&
                resolved[3] == 'v' && resolved[4] == '/' && resolved[5] == 't' &&
                resolved[6] == 't' && resolved[7] == 'y' && resolved[8] == '\0' &&
                !proc::has_controlling_terminal())
                return cq_post_guarded(ring, s.user_data, -ENXIO);
            const fs::file_mode existing_mode = fs::node_mode(resolved);
            if (existing_mode.exists() && existing_mode.is_directory() &&
                (s.op_flags & ASTIA_AIO_O_DIRECTORY) == 0)
                return cq_post_guarded(ring, s.user_data, -EISDIR);
            if (existing_mode.exists() && !existing_mode.is_directory() &&
                (s.op_flags & ASTIA_AIO_O_DIRECTORY) != 0)
                return cq_post_guarded(ring, s.user_data, -ENOTDIR);
            if (!existing_mode.exists() && (s.op_flags & ASTIA_AIO_O_CREAT) == 0)
                return cq_post_guarded(ring, s.user_data, -ENOENT);
            if (existing_mode.exists()) {
                auto*       cred  = proc::current_process();
                const uid_t owner = fs::node_uid(resolved);
                const gid_t group = fs::node_gid(resolved);
                if ((s.op_flags & ASTIA_AIO_O_RDWR) != 0 &&
                    !cred->can_access(existing_mode, owner, group,
                                      fs::permission_bits::write))
                    return cq_post_guarded(ring, s.user_data, -EACCES);
                if ((s.op_flags & ASTIA_AIO_O_WRONLY) == 0 &&
                    !cred->can_access(existing_mode, owner, group,
                                      fs::permission_bits::read))
                    return cq_post_guarded(ring, s.user_data, -EACCES);
            }
            mm::shptr<fs::open_file> object = existing_mode.is_directory()
                                                  ? fs::open_directory(resolved)
                                                  : fs::open(resolved, s.op_flags);
            if (!object)
                return cq_post_guarded(ring, s.user_data, -ENOENT);
            if ((s.op_flags & ASTIA_AIO_O_CREAT) != 0 && !existing_mode.exists()) {
                auto requested = mapper<fs::file_permissions>::from(
                    s.aux == 0 ? fs::default_file_permissions.raw() : s.aux);
                if (!requested.has_value)
                    return cq_post_guarded(ring, s.user_data, -EINVAL);
                (void) fs::chmod_path(resolved, requested.value);
                (void) fs::chown_path(resolved, uid_t{current->uid()},
                                      gid_t{current->gid()});
            }
            current = proc::current_process();
            if (current == nullptr)
                return cq_post_guarded(ring, s.user_data, -ESRCH);
            for (i32 i = 0; i < static_cast<i32>(proc::max_process_fds); ++i) {
                if (!current->descriptor(fd_t{i}) &&
                    current->set_descriptor(fd_t{i}, object,
                                            (s.op_flags & ASTIA_AIO_O_CLOEXEC) != 0
                                                ? static_cast<u32>(ASTIA_FD_CLOEXEC)
                                                : 0u))
                    return cq_post_guarded(ring, s.user_data, i);
            }
            return cq_post_guarded(ring, s.user_data, -EMFILE);
        }

    } // namespace

    struct openat : handler<openat, opcode::openat> {
        static bool invoke([[maybe_unused]] ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
            return handle_openat(ring, sqe);
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor openat_descriptor = openat::descriptor;

} // namespace astia::io::aio::operations
