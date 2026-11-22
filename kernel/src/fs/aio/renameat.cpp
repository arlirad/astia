#include "../../io/aio/handler.hpp"
#include "common.hpp"

namespace astia::io::aio::operations {

    namespace {

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

    } // namespace

    struct renameat : handler<renameat, opcode::renameat> {
        static bool invoke([[maybe_unused]] ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
            return handle_renameat(slot, ring, sqe);
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor renameat_descriptor = renameat::descriptor;

} // namespace astia::io::aio::operations
