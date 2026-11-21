#include "aio_dispatch.hpp"

#include <astia/api/errno.h>

#include "../io/aio/dispatch.hpp"
#include "../io/aio/memory.hpp"
#include "../proc/mapping.hpp"
#include "../proc/proc.hpp"
#include "mapping.hpp"

namespace astia::io::aio::filesystem {

    using namespace ring;
    using dispatch::file_for;
    using dispatch::parent_writable;
    using dispatch::resolve_at;

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

        bool handle_mkdirat(ring_slot&, astia_rings* ring, const astia_sqe& s) {
            char path[256];
            if (!memory::copy_in_string(span<char>(path, sizeof path),
                                        memory::user_ptr<char>(s.address)))
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            auto* current = proc::current_process();
            char  resolved[256];
            if (current == nullptr)
                return cq_post_guarded(ring, s.user_data, -ESRCH);
            long resolve_rc = resolve_at(current, s.fd, path, resolved, sizeof resolved);
            if (resolve_rc >= 0)
                resolve_rc = fs::resolve_path(resolved, resolved, sizeof resolved, true);
            if (resolve_rc < 0)
                return cq_post_guarded(ring, s.user_data, (i32) resolve_rc);
            auto requested = mapper<fs::file_mode>::from(
                s.aux == 0 ? fs::default_directory_permissions.raw() : s.aux);
            if (!requested.has_value)
                return cq_post_guarded(ring, s.user_data, -EINVAL);
            const long rc = fs::make_directory(resolved, requested.value);
            if (rc >= 0)
                (void) fs::chown_path(resolved, uid_t{current->uid()},
                                      gid_t{current->gid()});
            return cq_post_guarded(ring, s.user_data, static_cast<i32>(rc));
        }

        bool handle_unlinkat(ring_slot&, astia_rings* ring, const astia_sqe& s) {
            char path[256];
            if (!memory::copy_in_string(span<char>(path, sizeof path),
                                        memory::user_ptr<char>(s.address)))
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            auto* current = proc::current_process();
            char  resolved[256];
            if (current == nullptr)
                return cq_post_guarded(ring, s.user_data, -ESRCH);
            long resolve_rc = resolve_at(current, s.fd, path, resolved, sizeof resolved);
            if (resolve_rc >= 0)
                resolve_rc = fs::resolve_path(resolved, resolved, sizeof resolved, false);
            if (resolve_rc < 0)
                return cq_post_guarded(ring, s.user_data, (i32) resolve_rc);
            const long rc =
                fs::remove_path(resolved, (s.op_flags & UNLINKAT_REMOVEDIR) != 0);
            return cq_post_guarded(ring, s.user_data, static_cast<i32>(rc));
        }

        bool handle_renameat(ring_slot&, astia_rings* ring, const astia_sqe& s) {
            char old_path[256], new_path[256];
            if (!memory::copy_in_string(span<char>(old_path, sizeof old_path),
                                        memory::user_ptr<char>(s.address)) ||
                !memory::copy_in_string(span<char>(new_path, sizeof new_path),
                                        memory::user_ptr<char>(s.offset)))
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            auto* current = proc::current_process();
            char  old_resolved[256], new_resolved[256];
            if (current == nullptr)
                return cq_post_guarded(ring, s.user_data, -ESRCH);
            long old_rc =
                resolve_at(current, s.fd, old_path, old_resolved, sizeof old_resolved);
            i32  new_dirfd = s.length == 0 ? AT_FDCWD : static_cast<i32>(s.length);
            long new_rc    = resolve_at(current, new_dirfd, new_path, new_resolved,
                                        sizeof new_resolved);
            if (old_rc < 0)
                return cq_post_guarded(ring, s.user_data, (i32) old_rc);
            if (new_rc < 0)
                return cq_post_guarded(ring, s.user_data, (i32) new_rc);
            if (!parent_writable(current, old_resolved) ||
                !parent_writable(current, new_resolved))
                return cq_post_guarded(ring, s.user_data, -EACCES);
            return cq_post_guarded(
                ring, s.user_data,
                static_cast<i32>(fs::rename_path(old_resolved, new_resolved)));
        }

        bool handle_linkat(ring_slot&, astia_rings* ring, const astia_sqe& s) {
            char old_path[256], new_path[256];
            if (!memory::copy_in_string(span<char>(old_path, sizeof old_path),
                                        memory::user_ptr<char>(s.address)) ||
                !memory::copy_in_string(span<char>(new_path, sizeof new_path),
                                        memory::user_ptr<char>(s.offset)))
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            auto* current = proc::current_process();
            if (current == nullptr)
                return cq_post_guarded(ring, s.user_data, -ESRCH);
            char old_resolved[256], new_resolved[256];
            long old_rc =
                resolve_at(current, s.fd, old_path, old_resolved, sizeof old_resolved);
            i32  new_dirfd = s.length == 0 ? AT_FDCWD : static_cast<i32>(s.length);
            long new_rc    = resolve_at(current, new_dirfd, new_path, new_resolved,
                                        sizeof new_resolved);
            if (old_rc < 0)
                return cq_post_guarded(ring, s.user_data, (i32) old_rc);
            if (new_rc < 0)
                return cq_post_guarded(ring, s.user_data, (i32) new_rc);
            old_rc =
                fs::resolve_path(old_resolved, old_resolved, sizeof old_resolved, true);
            new_rc =
                fs::resolve_path(new_resolved, new_resolved, sizeof new_resolved, false);
            if (old_rc < 0)
                return cq_post_guarded(ring, s.user_data, (i32) old_rc);
            if (new_rc < 0)
                return cq_post_guarded(ring, s.user_data, (i32) new_rc);
            if (!parent_writable(current, old_resolved) ||
                !parent_writable(current, new_resolved))
                return cq_post_guarded(ring, s.user_data, -EACCES);
            return cq_post_guarded(
                ring, s.user_data,
                static_cast<i32>(fs::link_path(old_resolved, new_resolved)));
        }

        bool handle_statx(astia_rings* ring, const astia_sqe& s) {
            // Footnote ‡ of <astia/api/aio.h>: address NULL means fd is
            // the stat target (fstat), otherwise address is a pathname
            // and fd is the dirfd. The VFS still has one exact-path
            // namespace, so the dirfd is accepted and ignored.
            const auto out = memory::user_ptr<astia_stat>(s.offset).validate();
            if (!out) {
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            }
            if (!memory::user_span_writable_ok_for(
                    proc::current_process(),
                    span<u8>::bytes(s.offset, sizeof(astia_stat)))) {
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            }
            if (s.length != sizeof(astia_stat)) {
                return cq_post_guarded(ring, s.user_data, -EINVAL);
            }

            mm::shptr<fs::open_file> object;
            if (s.address != 0) {
                char path[256];
                if (!memory::copy_in_string(span<char>(path, sizeof path),
                                            memory::user_ptr<char>(s.address))) {
                    return cq_post_guarded(ring, s.user_data, -EFAULT);
                }
                auto* current = proc::current_process();
                char  resolved[256];
                if (current == nullptr)
                    return cq_post_guarded(ring, s.user_data, -EINVAL);
                long resolve_rc = resolve_at(current, s.fd == -1 ? AT_FDCWD : s.fd, path,
                                             resolved, sizeof resolved);
                if (resolve_rc >= 0)
                    resolve_rc = fs::resolve_path(resolved, resolved, sizeof resolved,
                                                  (s.op_flags & STATX_NOFOLLOW) == 0);
                if (resolve_rc < 0)
                    return cq_post_guarded(ring, s.user_data, (i32) resolve_rc);
                const fs::file_type type = fs::node_type(resolved);
                if ((s.op_flags & STATX_NOFOLLOW) != 0 && type == fs::directory_lnk) {
                    __builtin_memset(reinterpret_cast<void*>(out.value.get()), 0,
                                     sizeof(astia_stat));
                    auto* link = reinterpret_cast<astia_stat*>(out.value.get());
                    char  target[256]{};
                    long  n = fs::read_symlink(resolved, target, sizeof target);
                    if (n < 0)
                        return cq_post_guarded(ring, s.user_data, static_cast<i32>(n));
                    link->dev   = 1;
                    link->ino   = 1;
                    link->nlink = 1;
                    link->size  = (u64) n;
                    link->mode  = mapper<fs::file_mode>::to(fs::node_mode(resolved));
                    link->uid   = static_cast<u32>(fs::node_uid(resolved));
                    link->gid   = static_cast<u32>(fs::node_gid(resolved));
                    return cq_post_guarded(ring, s.user_data, 0);
                }
                object = fs::open_path(resolved);
                if (type == fs::directory_dir) {
                    __builtin_memset(reinterpret_cast<void*>(out.value.get()), 0,
                                     sizeof(astia_stat));
                    auto* directory  = reinterpret_cast<astia_stat*>(out.value.get());
                    directory->dev   = 1;
                    directory->ino   = 1;
                    directory->nlink = 1;
                    directory->mode  = mapper<fs::file_mode>::to(fs::node_mode(resolved));
                    directory->uid   = static_cast<u32>(fs::node_uid(resolved));
                    directory->gid   = static_cast<u32>(fs::node_gid(resolved));
                    return cq_post_guarded(ring, s.user_data, 0);
                }
                if (!object) {
                    char          child[256]{};
                    fs::file_type child_type = fs::directory_unknown;
                    if (fs::read_directory(resolved, 0, child, sizeof child,
                                           &child_type) <= 0) {
                        return cq_post_guarded(ring, s.user_data, -ENOENT);
                    }
                    __builtin_memset(reinterpret_cast<void*>(out.value.get()), 0,
                                     sizeof(astia_stat));
                    auto* directory  = reinterpret_cast<astia_stat*>(out.value.get());
                    directory->dev   = 1;
                    directory->ino   = 1;
                    directory->nlink = 1;
                    directory->mode  = mapper<fs::file_mode>::to(fs::node_mode(resolved));
                    return cq_post_guarded(ring, s.user_data, 0);
                }
            }
            else {
                object = file_for(s.fd);
                if (!object) {
                    return cq_post_guarded(ring, s.user_data, -EBADF);
                }
            }

            const long size = object->size();
            if (size < 0) {
                return cq_post_guarded(ring, s.user_data, static_cast<i32>(size));
            }
            // The VFS carries a size and nothing else about a node, so
            // mode/uid/times stay zero rather than being guessed.  The
            // range was validated above; this is the write it licensed.
            auto* stat_out = reinterpret_cast<astia_stat*>(out.value.get());
            __builtin_memset(stat_out, 0, sizeof *stat_out);
            stat_out->dev        = 1;
            stat_out->ino        = object->node()->inode();
            stat_out->nlink      = object->node()->nlink();
            stat_out->size       = static_cast<u64>(size);
            stat_out->blocks     = (static_cast<u64>(size) + 511u) / 512u;
            stat_out->mode       = mapper<fs::file_mode>::to(object->mode());
            stat_out->uid        = static_cast<u32>(object->node()->uid());
            stat_out->gid        = static_cast<u32>(object->node()->gid());
            stat_out->mtime_sec  = object->mtime_sec();
            stat_out->mtime_nsec = object->mtime_nsec();
            return cq_post_guarded(ring, s.user_data, 0);
        }

        bool handle_chmod(astia_rings* ring, const astia_sqe& s) {
            auto* current = proc::current_process();
            if (current == nullptr)
                return cq_post_guarded(ring, s.user_data, -ESRCH);
            auto requested = mapper<fs::file_permissions>::from(
                s.address == 0 ? s.aux & fs::file_permissions_mask : s.aux);
            if (!requested.has_value)
                return cq_post_guarded(ring, s.user_data, -EINVAL);
            if (s.address == 0) {
                auto mapped_fd = mapper<fd_t>::from(s.fd);
                if (!mapped_fd.has_value)
                    return cq_post_guarded(ring, s.user_data, -EBADF);
                auto object = current->descriptor(mapped_fd.value);
                if (!object)
                    return cq_post_guarded(ring, s.user_data, -EBADF);
                if (!object->node())
                    return cq_post_guarded(ring, s.user_data, -ENOSYS);
                if (current->euid() != 0 &&
                    current->euid() != static_cast<u32>(object->node()->uid()))
                    return cq_post_guarded(ring, s.user_data, -EPERM);
                return cq_post_guarded(ring, s.user_data,
                                       static_cast<i32>(object->chmod(requested.value)));
            }
            char path[256];
            if (!memory::copy_in_string(span<char>(path, sizeof path),
                                        memory::user_ptr<char>(s.address)))
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            char resolved[256];
            if (current == nullptr ||
                current->compose_path(path, resolved, sizeof resolved) < 0)
                return cq_post_guarded(ring, s.user_data, -ESRCH);
            if (current->euid() != 0 &&
                current->euid() != static_cast<u32>(fs::node_uid(resolved)))
                return cq_post_guarded(ring, s.user_data, -EPERM);
            return cq_post_guarded(
                ring, s.user_data,
                static_cast<i32>(fs::chmod_path(resolved, requested.value)));
        }

        bool handle_chown(astia_rings* ring, const astia_sqe& s) {
            auto* current = proc::current_process();
            auto  owner   = mapper<uid_t>::from(static_cast<i64>(s.aux));
            auto  group   = mapper<gid_t>::from(static_cast<i64>(s.offset));
            if (!owner.has_value || !group.has_value)
                return cq_post_guarded(ring, s.user_data, -EINVAL);
            if (current == nullptr)
                return cq_post_guarded(ring, s.user_data, -ESRCH);
            if (s.fd >= 0) {
                auto mapped_fd = mapper<fd_t>::from(s.fd);
                if (!mapped_fd.has_value)
                    return cq_post_guarded(ring, s.user_data, -EBADF);
                auto object = current->descriptor(mapped_fd.value);
                if (!object)
                    return cq_post_guarded(ring, s.user_data, -EBADF);
                if (current->euid() != 0 &&
                    (current->euid() != static_cast<u32>(object->node()->uid()) ||
                     owner.value != object->node()->uid() ||
                     !current->in_group(group.value)))
                    return cq_post_guarded(ring, s.user_data, -EPERM);
                return cq_post_guarded(
                    ring, s.user_data,
                    static_cast<i32>(object->node()->chown(owner.value, group.value)));
            }
            char path[256];
            if (!memory::copy_in_string(span<char>(path, sizeof path),
                                        memory::user_ptr<char>(s.address)))
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            char       resolved[256];
            const bool follow_final = (s.op_flags & STATX_NOFOLLOW) == 0;
            long resolve_rc = current->compose_path(path, resolved, sizeof resolved);
            if (resolve_rc >= 0)
                resolve_rc =
                    fs::resolve_path(resolved, resolved, sizeof resolved, follow_final);
            if (resolve_rc < 0)
                return cq_post_guarded(ring, s.user_data, (i32) resolve_rc);
            const uid_t existing_uid = fs::node_uid(resolved);
            if (current->euid() != 0 &&
                (current->euid() != static_cast<u32>(existing_uid) ||
                 owner.value != existing_uid || !current->in_group(group.value)))
                return cq_post_guarded(ring, s.user_data, -EPERM);
            return cq_post_guarded(
                ring, s.user_data,
                static_cast<i32>(
                    fs::chown_path(resolved, owner.value, group.value, follow_final)));
        }

        bool handle_utimensat(astia_rings* ring, const astia_sqe& s) {
            char path[256];
            if (!memory::copy_in_string(span<char>(path, sizeof path),
                                        memory::user_ptr<char>(s.address)))
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            auto* current = proc::current_process();
            char  resolved[256];
            if (current == nullptr)
                return cq_post_guarded(ring, s.user_data, -ESRCH);
            long resolve_rc = current->compose_path(path, resolved, sizeof resolved);
            if (resolve_rc >= 0)
                resolve_rc = fs::resolve_path(resolved, resolved, sizeof resolved, true);
            if (resolve_rc < 0)
                return cq_post_guarded(ring, s.user_data, (i32) resolve_rc);
            u64 sec  = 0;
            u64 nsec = 0;
            if (!memory::realtime_now(sec, nsec))
                return cq_post_guarded(ring, s.user_data, -EIO);
            return cq_post_guarded(ring, s.user_data,
                                   static_cast<i32>(fs::touch_path(resolved, sec, nsec)));
        }

        bool handle_symlinkat(astia_rings* ring, const astia_sqe& s) {
            char target[256], path[256];
            if (!memory::copy_in_string(span<char>(target, sizeof target),
                                        memory::user_ptr<char>(s.address)) ||
                !memory::copy_in_string(span<char>(path, sizeof path),
                                        memory::user_ptr<char>(s.offset)))
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            auto* current = proc::current_process();
            char  resolved[256];
            if (!current)
                return cq_post_guarded(ring, s.user_data, -ESRCH);
            long resolve_rc = resolve_at(current, s.fd, path, resolved, sizeof resolved);
            if (resolve_rc < 0)
                return cq_post_guarded(ring, s.user_data, (i32) resolve_rc);
            resolve_rc = fs::resolve_path(resolved, resolved, sizeof resolved, false);
            if (resolve_rc < 0)
                return cq_post_guarded(ring, s.user_data, (i32) resolve_rc);
            long link_rc = fs::make_symlink(target, resolved);
            if (link_rc >= 0)
                (void) fs::chown_path(resolved, uid_t{current->uid()},
                                      gid_t{current->gid()}, false);
            return cq_post_guarded(ring, s.user_data, (i32) link_rc);
        }

        bool handle_readlinkat(astia_rings* ring, const astia_sqe& s) {
            char path[256];
            if (!memory::copy_in_string(span<char>(path, sizeof path),
                                        memory::user_ptr<char>(s.address)))
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            auto* current = proc::current_process();
            char  resolved[256];
            if (!current)
                return cq_post_guarded(ring, s.user_data, -ESRCH);
            long resolve_rc = resolve_at(current, s.fd, path, resolved, sizeof resolved);
            if (resolve_rc < 0)
                return cq_post_guarded(ring, s.user_data, (i32) resolve_rc);
            resolve_rc = fs::resolve_path(resolved, resolved, sizeof resolved, false);
            if (resolve_rc < 0)
                return cq_post_guarded(ring, s.user_data, (i32) resolve_rc);
            auto out = span<u8>::bytes(s.offset, s.length);
            if (!memory::user_span_ok_for(out))
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            // read_symlink is a kernel/VFS API; never give it a raw
            // caller address.  The fixed path limit is also the VFS's
            // maximum link length, so an oversized user capacity is
            // harmless and cannot become a kernel stack allocation.
            char       target[256]{};
            const u32  capacity = s.length < sizeof target ? s.length : sizeof target;
            const long n        = fs::read_symlink(resolved, target, capacity);
            if (n < 0)
                return cq_post_guarded(ring, s.user_data, (i32) n);
            if (n != 0 && !memory::copy_out(
                              span<u8>::bytes(s.offset, static_cast<usize>(n)), target))
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            return cq_post_guarded(ring, s.user_data, (i32) n);
        }

        bool handle_readdir(astia_rings* ring, const astia_sqe& s) {
            char path[256];
            if (!memory::copy_in_string(span<char>(path, sizeof path),
                                        memory::user_ptr<char>(s.offset)))
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            auto* current = proc::current_process();
            char  resolved[256];
            if (current == nullptr)
                return cq_post_guarded(ring, s.user_data, -ESRCH);
            long resolve_rc = current->compose_path(path, resolved, sizeof resolved);
            if (resolve_rc >= 0)
                resolve_rc = fs::resolve_path(resolved, resolved, sizeof resolved, true);
            if (resolve_rc < 0)
                return cq_post_guarded(ring, s.user_data, (i32) resolve_rc);
            const auto out = memory::user_ptr<astia_dirent>(s.address).validate();
            if (!out)
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            astia_dirent  result{};
            fs::file_type type = fs::directory_unknown;
            long rc     = fs::read_directory(resolved, s.fd < 0 ? 0 : s.fd, result.name,
                                             sizeof result.name, &type);
            result.type = static_cast<u32>(type);
            if (rc > 0) {
                u32 length = 0;
                while (length < sizeof result.name && result.name[length] != '\0')
                    ++length;
                result.namelen = length;
            }
            if (!memory::copy_out(out.value.bytes(), &result))
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            return cq_post_guarded(ring, s.user_data, static_cast<i32>(rc));
        }

        bool handle_close(astia_rings* ring, const astia_sqe& s) {
            proc::process* current = proc::current_process();
            auto           mapped  = mapper<fd_t>::from(s.fd);
            if (current == nullptr || !mapped.has_value ||
                !current->descriptor(mapped.value)) {
                return cq_post_guarded(ring, s.user_data, -EBADF);
            }
            current->clear_descriptor(mapped.value);
            return cq_post_guarded(ring, s.user_data, 0);
        }

        bool handle_ftruncate(astia_rings* ring, const astia_sqe& s) {
            auto object = file_for(s.fd);
            if (!object)
                return cq_post_guarded(ring, s.user_data, -EBADF);
            if ((object->flags() & fs::open_file_access_mask) == 0)
                return cq_post_guarded(ring, s.user_data, -EBADF);
            return cq_post_guarded(ring, s.user_data,
                                   static_cast<i32>(object->truncate(s.offset)));
        }

        bool handle_fsync(astia_rings* ring, const astia_sqe& s) {
            auto object = file_for(s.fd);
            if (!object)
                return cq_post_guarded(ring, s.user_data, -EBADF);
            if (!object->mode().is_regular())
                return cq_post_guarded(ring, s.user_data, -EINVAL);
            return cq_post_guarded(ring, s.user_data, static_cast<i32>(object->sync()));
        }

    } // namespace

    bool dispatch(ring_slot& slot, astia_rings* ring, const astia_sqe& s) {
        switch (s.op) {
        case OP_OPENAT:
            return handle_openat(ring, s);
        case OP_CLOSE:
            return handle_close(ring, s);
        case OP_STATX:
            return handle_statx(ring, s);
        case OP_FSYNC:
            return handle_fsync(ring, s);
        case OP_UNLINKAT:
            return handle_unlinkat(slot, ring, s);
        case OP_MKDIRAT:
            return handle_mkdirat(slot, ring, s);
        case OP_RENAMEAT:
            return handle_renameat(slot, ring, s);
        case OP_READDIR:
            return handle_readdir(ring, s);
        case OP_CHMOD:
            return handle_chmod(ring, s);
        case OP_UTIMENSAT:
            return handle_utimensat(ring, s);
        case OP_SYMLINKAT:
            return handle_symlinkat(ring, s);
        case OP_READLINKAT:
            return handle_readlinkat(ring, s);
        case OP_CHOWN:
            return handle_chown(ring, s);
        case OP_LINKAT:
            return handle_linkat(slot, ring, s);
        case OP_FTRUNCATE:
            return handle_ftruncate(ring, s);
        default:
            return false;
        }
    }

} // namespace astia::io::aio::filesystem
