#include "../../io/aio/handler.hpp"
#include "common.hpp"

namespace astia::io::aio::operations {
    namespace {
        /// st_ino for an open handle: the backing file's own inode, with
        /// the VFS node identity taking over for filesystems whose file
        /// objects carry none.
        u64 handle_inode(const fs::open_file& handle) {
            const u64 inode = handle.parent() ? handle.parent()->inode() : 0;

            return inode != 0 ? inode : fs::node_inode(handle.path());
        }
    } // namespace

    struct statx : handler<statx, opcode::statx> {
        static core::task<aio_result>
        invoke([[maybe_unused]] ring::ring_slot& slot, [[maybe_unused]] astia_rings* ring,
               astia_sqe sqe, [[maybe_unused]] core::cltoken token) noexcept {
            // Footnote ‡ of <astia/api/aio.h>: address NULL means fd is
            // the stat target (fstat), otherwise address is a pathname
            // and fd is the dirfd. The VFS still has one exact-path
            // namespace, so the dirfd is accepted and ignored.
            const auto out = memory::user_ptr<astia_stat>(sqe.offset).validate();
            if (!out)
                co_return -EFAULT;

            if (!memory::user_span_writable_ok_for(
                    proc::current_process(),
                    span<u8>::bytes(sqe.offset, sizeof(astia_stat))))
                co_return -EFAULT;

            if (sqe.length != sizeof(astia_stat))
                co_return -EINVAL;

            mm::shptr<fs::open_file> handle;

            if (sqe.address != 0) {
                char path[256];

                if (!memory::copy_in_string(span<char>(path, sizeof path),
                                            memory::user_ptr<char>(sqe.address)))
                    co_return -EFAULT;

                auto* current = proc::current_process();

                char resolved[256];

                if (current == nullptr)
                    co_return -EINVAL;

                long resolve_rc =
                    resolve_at(current, sqe.fd == -1 ? AT_FDCWD : sqe.fd, path, resolved,
                               sizeof resolved, (sqe.op_flags & STATX_NOFOLLOW) == 0);
                if (resolve_rc < 0)
                    co_return (i32) resolve_rc;

                const fs::file_type type = fs::node_type(resolved);

                if ((sqe.op_flags & STATX_NOFOLLOW) != 0 && type == fs::directory_lnk) {
                    __builtin_memset(reinterpret_cast<void*>(out.value.get()), 0,
                                     sizeof(astia_stat));

                    auto* link = reinterpret_cast<astia_stat*>(out.value.get());

                    char target[256]{};
                    long n = fs::read_symlink(resolved, target, sizeof target);
                    if (n < 0)
                        co_return static_cast<i32>(n);

                    link->dev   = fs::node_device(resolved);
                    link->ino   = fs::node_inode(resolved);
                    link->nlink = 1;
                    link->size  = (u64) n;
                    link->mode  = mapper<fs::file_mode>::to(fs::node_mode(resolved));
                    link->uid   = static_cast<u32>(fs::node_uid(resolved));
                    link->gid   = static_cast<u32>(fs::node_gid(resolved));

                    const fs::file_timestamps times = fs::node_timestamps(resolved);

                    link->atime_sec  = times.atime_sec;
                    link->atime_nsec = times.atime_nsec;
                    link->mtime_sec  = times.mtime_sec;
                    link->mtime_nsec = times.mtime_nsec;
                    link->ctime_sec  = times.ctime_sec;
                    link->ctime_nsec = times.ctime_nsec;

                    co_return 0;
                }

                handle = fs::open_path(resolved);

                if (type == fs::directory_dir) {
                    __builtin_memset(reinterpret_cast<void*>(out.value.get()), 0,
                                     sizeof(astia_stat));

                    auto* directory = reinterpret_cast<astia_stat*>(out.value.get());

                    directory->dev   = fs::node_device(resolved);
                    directory->ino   = fs::node_inode(resolved);
                    directory->nlink = 1;
                    directory->mode  = mapper<fs::file_mode>::to(fs::node_mode(resolved));
                    directory->uid   = static_cast<u32>(fs::node_uid(resolved));
                    directory->gid   = static_cast<u32>(fs::node_gid(resolved));

                    const fs::file_timestamps times = fs::node_timestamps(resolved);

                    directory->atime_sec  = times.atime_sec;
                    directory->atime_nsec = times.atime_nsec;
                    directory->mtime_sec  = times.mtime_sec;
                    directory->mtime_nsec = times.mtime_nsec;
                    directory->ctime_sec  = times.ctime_sec;
                    directory->ctime_nsec = times.ctime_nsec;

                    co_return 0;
                }

                if (!handle) {
                    char child[256]{};

                    fs::file_type child_type = fs::directory_unknown;

                    if (fs::read_directory(resolved, 0, child, sizeof child, &child_type,
                                           nullptr) <= 0)
                        co_return -ENOENT;

                    __builtin_memset(reinterpret_cast<void*>(out.value.get()), 0,
                                     sizeof(astia_stat));

                    auto* directory = reinterpret_cast<astia_stat*>(out.value.get());

                    directory->dev   = fs::node_device(resolved);
                    directory->ino   = fs::node_inode(resolved);
                    directory->nlink = 1;
                    directory->mode  = mapper<fs::file_mode>::to(fs::node_mode(resolved));

                    const fs::file_timestamps times = fs::node_timestamps(resolved);

                    directory->atime_sec  = times.atime_sec;
                    directory->atime_nsec = times.atime_nsec;
                    directory->mtime_sec  = times.mtime_sec;
                    directory->mtime_nsec = times.mtime_nsec;
                    directory->ctime_sec  = times.ctime_sec;
                    directory->ctime_nsec = times.ctime_nsec;

                    co_return 0;
                }
            }
            else {
                handle = file_for(sqe.fd);
                if (!handle)
                    co_return -EBADF;
            }

            const char* const path_only = path_only_path(*handle);

            if (!handle->parent() && path_only == nullptr)
                co_return -ENOSYS;

            const long size = handle->parent() ? handle->size() : 0;
            if (size < 0)
                co_return static_cast<i32>(size);

            // The range was validated above; this is the write it licensed.
            // Timestamps come from the backing file and stay at their
            // documented zero when the filesystem records none.
            auto* stat_out = reinterpret_cast<astia_stat*>(out.value.get());

            __builtin_memset(stat_out, 0, sizeof *stat_out);

            const fs::file_timestamps times = path_only != nullptr
                                                  ? fs::node_timestamps(path_only)
                                                  : handle->timestamps();

            stat_out->dev        = fs::node_device(*handle);
            stat_out->ino        = handle_inode(*handle);
            stat_out->nlink      = handle_nlink(*handle);
            stat_out->size       = static_cast<u64>(size);
            stat_out->blocks     = (static_cast<u64>(size) + 511u) / 512u;
            stat_out->mode       = mapper<fs::file_mode>::to(handle->mode());
            stat_out->uid        = static_cast<u32>(handle_uid(*handle));
            stat_out->gid        = static_cast<u32>(handle_gid(*handle));
            stat_out->atime_sec  = times.atime_sec;
            stat_out->atime_nsec = times.atime_nsec;
            stat_out->mtime_sec  = times.mtime_sec;
            stat_out->mtime_nsec = times.mtime_nsec;
            stat_out->ctime_sec  = times.ctime_sec;
            stat_out->ctime_nsec = times.ctime_nsec;

            co_return 0;
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor statx_descriptor = statx::descriptor;
} // namespace astia::io::aio::operations
