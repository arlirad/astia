#include "../../io/aio/handler.hpp"
#include "common.hpp"

namespace astia::io::aio::operations {
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

                    link->dev   = 1;
                    link->ino   = 1;
                    link->nlink = 1;
                    link->size  = (u64) n;
                    link->mode  = mapper<fs::file_mode>::to(fs::node_mode(resolved));
                    link->uid   = static_cast<u32>(fs::node_uid(resolved));
                    link->gid   = static_cast<u32>(fs::node_gid(resolved));

                    co_return 0;
                }

                handle = fs::open_path(resolved);

                if (type == fs::directory_dir) {
                    __builtin_memset(reinterpret_cast<void*>(out.value.get()), 0,
                                     sizeof(astia_stat));

                    auto* directory = reinterpret_cast<astia_stat*>(out.value.get());

                    directory->dev   = 1;
                    directory->ino   = 1;
                    directory->nlink = 1;
                    directory->mode  = mapper<fs::file_mode>::to(fs::node_mode(resolved));
                    directory->uid   = static_cast<u32>(fs::node_uid(resolved));
                    directory->gid   = static_cast<u32>(fs::node_gid(resolved));

                    co_return 0;
                }

                if (!handle) {
                    char child[256]{};

                    fs::file_type child_type = fs::directory_unknown;

                    if (fs::read_directory(resolved, 0, child, sizeof child,
                                           &child_type) <= 0)
                        co_return -ENOENT;

                    __builtin_memset(reinterpret_cast<void*>(out.value.get()), 0,
                                     sizeof(astia_stat));

                    auto* directory = reinterpret_cast<astia_stat*>(out.value.get());

                    directory->dev   = 1;
                    directory->ino   = 1;
                    directory->nlink = 1;
                    directory->mode  = mapper<fs::file_mode>::to(fs::node_mode(resolved));

                    co_return 0;
                }
            }
            else {
                handle = file_for(sqe.fd);
                if (!handle)
                    co_return -EBADF;
            }

            const long size = handle->size();
            if (size < 0)
                co_return static_cast<i32>(size);

            // The VFS carries a size and nothing else about a node, so
            // mode/uid/times stay zero rather than being guessed.  The
            // range was validated above; this is the write it licensed.
            auto* stat_out = reinterpret_cast<astia_stat*>(out.value.get());

            __builtin_memset(stat_out, 0, sizeof *stat_out);

            stat_out->dev        = 1;
            stat_out->ino        = handle->parent()->inode();
            stat_out->nlink      = handle->parent()->nlink();
            stat_out->size       = static_cast<u64>(size);
            stat_out->blocks     = (static_cast<u64>(size) + 511u) / 512u;
            stat_out->mode       = mapper<fs::file_mode>::to(handle->mode());
            stat_out->uid        = static_cast<u32>(handle->parent()->uid());
            stat_out->gid        = static_cast<u32>(handle->parent()->gid());
            stat_out->mtime_sec  = handle->mtime_sec();
            stat_out->mtime_nsec = handle->mtime_nsec();

            co_return 0;
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor statx_descriptor = statx::descriptor;
} // namespace astia::io::aio::operations
