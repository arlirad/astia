#include "../../io/aio/handler.hpp"
#include "common.hpp"

namespace astia::io::aio::operations {

    struct renameat : handler<renameat, opcode::renameat> {
        static core::task<aio_result> invoke([[maybe_unused]] ring::ring_slot& slot,
                                             [[maybe_unused]] astia_rings*     ring,
                                             astia_sqe sqe) noexcept {
            char old_path[256], new_path[256];
            if (!memory::copy_in_string(span<char>(old_path, sizeof old_path),
                                        memory::user_ptr<char>(sqe.address)) ||
                !memory::copy_in_string(span<char>(new_path, sizeof new_path),
                                        memory::user_ptr<char>(sqe.offset)))
                co_return -EFAULT;
            auto* current = proc::current_process();
            char  old_resolved[256], new_resolved[256];
            if (current == nullptr)
                co_return -ESRCH;
            long old_rc =
                resolve_at(current, sqe.fd, old_path, old_resolved, sizeof old_resolved);
            i32  new_dirfd = sqe.length == 0 ? AT_FDCWD : static_cast<i32>(sqe.length);
            long new_rc    = resolve_at(current, new_dirfd, new_path, new_resolved,
                                        sizeof new_resolved);
            if (old_rc < 0)
                co_return (i32) old_rc;
            if (new_rc < 0)
                co_return (i32) new_rc;
            if (!parent_writable(current, old_resolved) ||
                !parent_writable(current, new_resolved))
                co_return -EACCES;
            co_return static_cast<i32>(fs::rename_path(old_resolved, new_resolved));
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor renameat_descriptor = renameat::descriptor;

} // namespace astia::io::aio::operations
