#include "../../io/aio/handler.hpp"
#include "common.hpp"

namespace astia::io::aio::operations {

    struct symlinkat : handler<symlinkat, opcode::symlinkat> {
        static bool invoke([[maybe_unused]] ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
            char target[256], path[256];
            if (!memory::copy_in_string(span<char>(target, sizeof target),
                                        memory::user_ptr<char>(sqe.address)) ||
                !memory::copy_in_string(span<char>(path, sizeof path),
                                        memory::user_ptr<char>(sqe.offset)))
                return cq_post_guarded(ring, sqe.user_data, -EFAULT);
            auto* current = proc::current_process();
            char  resolved[256];
            if (!current)
                return cq_post_guarded(ring, sqe.user_data, -ESRCH);
            long resolve_rc =
                resolve_at(current, sqe.fd, path, resolved, sizeof resolved);
            if (resolve_rc < 0)
                return cq_post_guarded(ring, sqe.user_data, (i32) resolve_rc);
            resolve_rc = fs::resolve_path(resolved, resolved, sizeof resolved, false);
            if (resolve_rc < 0)
                return cq_post_guarded(ring, sqe.user_data, (i32) resolve_rc);
            long link_rc = fs::make_symlink(target, resolved);
            if (link_rc >= 0)
                (void) fs::chown_path(resolved, uid_t{current->uid()},
                                      gid_t{current->gid()}, false);
            return cq_post_guarded(ring, sqe.user_data, (i32) link_rc);
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor symlinkat_descriptor = symlinkat::descriptor;

} // namespace astia::io::aio::operations
