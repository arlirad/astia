#include "../../io/aio/handler.hpp"
#include "../events.hpp"
#include "common.hpp"

namespace astia::io::aio::operations {
    namespace {
        bool access_allowed(const proc::process& cred, const fs::file_mode& mode,
                            uid_t owner, gid_t group, u32 access) {
            if ((access == ASTIA_AIO_O_WRONLY || access == ASTIA_AIO_O_RDWR) &&
                !cred.can_access(mode, owner, group, fs::permission_bits::write))
                return false;

            if ((access == ASTIA_AIO_O_RDONLY || access == ASTIA_AIO_O_RDWR) &&
                !cred.can_access(mode, owner, group, fs::permission_bits::read))
                return false;

            return true;
        }
    } // namespace

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

            // A directory-only open cannot also demand creation: the create
            // branch publishes regular files, so the combination is invalid
            // regardless of whether the target already exists.
            if ((sqe.op_flags & (ASTIA_AIO_O_DIRECTORY | ASTIA_AIO_O_CREAT)) ==
                (ASTIA_AIO_O_DIRECTORY | ASTIA_AIO_O_CREAT))
                co_return -EINVAL;

            auto* current = proc::current_process();

            char resolved[256];

            if (current == nullptr)
                co_return -EINVAL;

            // O_CREAT|O_EXCL must observe the final component itself: an
            // existing symlink is the conflict, never a path to create
            // through.
            const bool exclusive = (sqe.op_flags & ASTIA_AIO_O_CREAT) != 0 &&
                                   (sqe.op_flags & ASTIA_AIO_O_EXCL) != 0;
            const bool nofollow = (sqe.op_flags & ASTIA_AIO_O_NOFOLLOW) != 0;

            long resolve_rc =
                resolve_at(current, sqe.fd == -1 ? AT_FDCWD : sqe.fd, path, resolved,
                           sizeof resolved, !(exclusive || nofollow));
            if (resolve_rc < 0)
                co_return (i32) resolve_rc;

            if (nofollow && fs::node_type(resolved) == fs::directory_lnk)
                co_return -ELOOP;

            if (resolved[0] == '/' && resolved[1] == 'd' && resolved[2] == 'e' &&
                resolved[3] == 'v' && resolved[4] == '/' && resolved[5] == 't' &&
                resolved[6] == 't' && resolved[7] == 'y' && resolved[8] == '\0' &&
                !proc::has_controlling_terminal())
                co_return -ENXIO;

            const fs::file_mode existing_mode = fs::node_mode(resolved);

            if (exclusive && existing_mode.exists())
                co_return -EEXIST;

            if (existing_mode.exists() && existing_mode.is_directory() &&
                (sqe.op_flags & ASTIA_AIO_O_DIRECTORY) == 0)
                co_return -EISDIR;

            if (existing_mode.exists() && !existing_mode.is_directory() &&
                (sqe.op_flags & ASTIA_AIO_O_DIRECTORY) != 0)
                co_return -ENOTDIR;

            if (!existing_mode.exists() && (sqe.op_flags & ASTIA_AIO_O_CREAT) == 0)
                co_return -ENOENT;

            // A create or write on an immutable mount is a read-only failure,
            // not a missing path.  The mount's open() has no create branch, so
            // without this a new file under /bin would surface as ENOENT.
            const bool needs_mutation =
                (sqe.op_flags &
                 (ASTIA_AIO_O_WRONLY | ASTIA_AIO_O_RDWR | ASTIA_AIO_O_TRUNC)) != 0 ||
                (!existing_mode.exists() && (sqe.op_flags & ASTIA_AIO_O_CREAT) != 0);

            if (needs_mutation && fs::read_only_path(resolved))
                co_return -EROFS;

            if (!existing_mode.exists() && (sqe.op_flags & ASTIA_AIO_O_CREAT) != 0 &&
                !parent_writable(current, resolved))
                co_return -EACCES;

            // The identity is captured once, from the resolution the checks
            // above used.  The backend must bind this same node; a name that
            // changed after the checks fails with -EAGAIN.
            const fs::node_identity checked = existing_mode.exists()
                                                  ? fs::node_identity_of(resolved)
                                                  : fs::node_identity{};

            const uid_t existing_uid =
                existing_mode.exists() ? fs::node_uid(resolved) : uid_t{};
            const gid_t existing_gid =
                existing_mode.exists() ? fs::node_gid(resolved) : gid_t{};

            const u32 access = sqe.op_flags & ASTIA_AIO_O_ACCMODE;

            if (existing_mode.exists() &&
                !access_allowed(*current, existing_mode, existing_uid, existing_gid,
                                access))
                co_return -EACCES;

            fs::events::open_checked::emit(resolved);

            long open_error = 0;

            const bool creating =
                !existing_mode.exists() && (sqe.op_flags & ASTIA_AIO_O_CREAT) != 0;

            mm::shptr<fs::open_file> handle;

            if (existing_mode.is_directory()) {
                handle = fs::open_directory_verified(resolved, checked);
            }
            else if (creating) {
                const auto requested = mapper<fs::file_permissions>::from(
                    sqe.aux == 0 ? fs::default_file_permissions.raw() : sqe.aux);

                if (!requested.has_value)
                    co_return -EINVAL;

                const fs::creation_attributes attributes{
                    fs::apply_umask(requested.value, current->umask()),
                    uid_t{current->uid()}, gid_t{current->gid()}};

                handle = fs::open_creating_verified(resolved, sqe.op_flags, checked,
                                                    attributes, &open_error);
            }
            else {
                handle = fs::open_verified(resolved, sqe.op_flags, checked, &open_error);
            }

            if (!handle) {
                if (exclusive)
                    co_return -EEXIST;

                co_return open_error != 0 ? open_error : -ENOENT;
            }

            // The decisive check runs on the object that was actually bound,
            // so a metadata read that raced the replacement cannot authorize
            // a different node.
            auto* cred = proc::current_process();
            if (cred == nullptr)
                co_return -ESRCH;

            if (!access_allowed(*cred, handle->mode(), handle_uid(*handle),
                                handle_gid(*handle), access))
                co_return -EACCES;

            current = proc::current_process();
            if (current == nullptr)
                co_return -ESRCH;

            const auto installed = current->allocate_descriptor(
                handle, (sqe.op_flags & ASTIA_AIO_O_CLOEXEC) != 0
                            ? static_cast<u32>(ASTIA_FD_CLOEXEC)
                            : 0u);

            co_return installed ? installed.value.value : -EMFILE;
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor openat_descriptor = openat::descriptor;
} // namespace astia::io::aio::operations
