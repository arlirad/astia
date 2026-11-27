#include "../../io/aio/handler.hpp"
#include "common.hpp"

namespace astia::io::aio::operations {
    struct chown : handler<chown, opcode::chown> {
        static constexpr u32 unchanged_id = 0xffffffffu;

        static constexpr bool is_unchanged(u32 raw) noexcept {
            return raw == unchanged_id;
        }

        static constexpr bool is_unchanged(u64 raw) noexcept {
            return raw == static_cast<u64>(unchanged_id) ||
                   raw == static_cast<u64>(static_cast<i64>(-1));
        }

        static bool may_change(proc::process* current, uid_t existing_uid,
                               bool owner_unchanged, uid_t owner, bool group_unchanged,
                               gid_t group) noexcept {
            if (current->euid() == 0)
                return true;

            if (current->euid() != static_cast<u32>(existing_uid))
                return false;

            if (!owner_unchanged && owner != existing_uid)
                return false;

            if (!group_unchanged && !current->in_group(group))
                return false;

            return true;
        }

        static core::task<aio_result>
        invoke([[maybe_unused]] ring::ring_slot& slot, [[maybe_unused]] astia_rings* ring,
               astia_sqe sqe, [[maybe_unused]] core::cltoken token) noexcept {
            auto* current = proc::current_process();

            const bool owner_unchanged = is_unchanged(sqe.aux);
            const bool group_unchanged = is_unchanged(static_cast<u64>(sqe.offset));

            uid_t owner{};
            gid_t group{};

            if (!owner_unchanged) {
                auto mapped = mapper<uid_t>::from(static_cast<i64>(sqe.aux));

                if (!mapped.has_value)
                    co_return -EINVAL;

                owner = mapped.value;
            }

            if (!group_unchanged) {
                auto mapped = mapper<gid_t>::from(static_cast<i64>(sqe.offset));

                if (!mapped.has_value)
                    co_return -EINVAL;

                group = mapped.value;
            }

            if (current == nullptr)
                co_return -ESRCH;

            if (sqe.fd >= 0) {
                auto mapped_fd = mapper<fd_t>::from(sqe.fd);

                if (!mapped_fd.has_value)
                    co_return -EBADF;

                auto handle = current->descriptor(mapped_fd.value);
                if (!handle)
                    co_return -EBADF;

                const uid_t existing_uid = handle->parent()->uid();
                const gid_t existing_gid = handle->parent()->gid();
                const uid_t target_uid   = owner_unchanged ? existing_uid : owner;
                const gid_t target_gid   = group_unchanged ? existing_gid : group;

                if (!may_change(current, existing_uid, owner_unchanged, owner,
                                group_unchanged, group))
                    co_return -EPERM;

                co_return static_cast<i32>(
                    handle->parent()->chown(target_uid, target_gid));
            }

            char path[256];

            if (!memory::copy_in_string(span<char>(path, sizeof path),
                                        memory::user_ptr<char>(sqe.address)))
                co_return -EFAULT;

            char       resolved[256];
            const bool follow_final = (sqe.op_flags & STATX_NOFOLLOW) == 0;
            const long resolve_rc =
                resolve_path_for(current, path, resolved, sizeof resolved, follow_final);

            if (resolve_rc < 0)
                co_return (i32) resolve_rc;

            const uid_t existing_uid = fs::node_uid(resolved);
            const gid_t existing_gid = fs::node_gid(resolved);
            const uid_t target_uid   = owner_unchanged ? existing_uid : owner;
            const gid_t target_gid   = group_unchanged ? existing_gid : group;

            if (!may_change(current, existing_uid, owner_unchanged, owner,
                            group_unchanged, group))
                co_return -EPERM;

            co_return static_cast<i32>(
                fs::chown_path(resolved, target_uid, target_gid, follow_final));
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor chown_descriptor = chown::descriptor;
} // namespace astia::io::aio::operations
