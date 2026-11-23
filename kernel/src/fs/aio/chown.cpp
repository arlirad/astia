#include "../../io/aio/handler.hpp"
#include "common.hpp"

namespace astia::io::aio::operations {

    struct chown : handler<chown, opcode::chown> {
        static core::task<aio_result> invoke([[maybe_unused]] ring::ring_slot& slot,
                                             [[maybe_unused]] astia_rings*     ring,
                                             astia_sqe sqe) noexcept {
            auto* current = proc::current_process();
            auto  owner   = mapper<uid_t>::from(static_cast<i64>(sqe.aux));
            auto  group   = mapper<gid_t>::from(static_cast<i64>(sqe.offset));
            if (!owner.has_value || !group.has_value)
                co_return -EINVAL;
            if (current == nullptr)
                co_return -ESRCH;
            if (sqe.fd >= 0) {
                auto mapped_fd = mapper<fd_t>::from(sqe.fd);
                if (!mapped_fd.has_value)
                    co_return -EBADF;
                auto object = current->descriptor(mapped_fd.value);
                if (!object)
                    co_return -EBADF;
                if (current->euid() != 0 &&
                    (current->euid() != static_cast<u32>(object->node()->uid()) ||
                     owner.value != object->node()->uid() ||
                     !current->in_group(group.value)))
                    co_return -EPERM;
                co_return static_cast<i32>(
                    object->node()->chown(owner.value, group.value));
            }
            char path[256];
            if (!memory::copy_in_string(span<char>(path, sizeof path),
                                        memory::user_ptr<char>(sqe.address)))
                co_return -EFAULT;
            char       resolved[256];
            const bool follow_final = (sqe.op_flags & STATX_NOFOLLOW) == 0;
            long resolve_rc = current->compose_path(path, resolved, sizeof resolved);
            if (resolve_rc >= 0)
                resolve_rc =
                    fs::resolve_path(resolved, resolved, sizeof resolved, follow_final);
            if (resolve_rc < 0)
                co_return (i32) resolve_rc;
            const uid_t existing_uid = fs::node_uid(resolved);
            if (current->euid() != 0 &&
                (current->euid() != static_cast<u32>(existing_uid) ||
                 owner.value != existing_uid || !current->in_group(group.value)))
                co_return -EPERM;
            co_return static_cast<i32>(
                fs::chown_path(resolved, owner.value, group.value, follow_final));
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor chown_descriptor = chown::descriptor;

} // namespace astia::io::aio::operations
