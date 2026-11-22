#include "../../io/aio/handler.hpp"
#ifdef ASTIA_AIO_HOST_TEST
#include "dispatch.hpp"
#else
#include "common.hpp"
#endif

namespace astia::io::aio::operations {

#ifndef ASTIA_AIO_HOST_TEST
    namespace {

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

    } // namespace
#endif

    struct statx : handler<statx, opcode::statx> {
        static bool invoke([[maybe_unused]] ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
#ifdef ASTIA_AIO_HOST_TEST
            return ::astia::io::aio::filesystem::dispatch(slot, ring, sqe);
#else
            return handle_statx(ring, sqe);
#endif
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor statx_descriptor = statx::descriptor;

} // namespace astia::io::aio::operations
