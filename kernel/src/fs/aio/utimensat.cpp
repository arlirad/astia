#include "../../io/aio/handler.hpp"
#include "common.hpp"

namespace astia::io::aio::operations {

    struct utimensat : handler<utimensat, opcode::utimensat> {
        static bool invoke([[maybe_unused]] ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
            char path[256];
            if (!memory::copy_in_string(span<char>(path, sizeof path),
                                        memory::user_ptr<char>(sqe.address)))
                return cq_post_guarded(ring, sqe.user_data, -EFAULT);
            auto* current = proc::current_process();
            char  resolved[256];
            if (current == nullptr)
                return cq_post_guarded(ring, sqe.user_data, -ESRCH);
            long resolve_rc = current->compose_path(path, resolved, sizeof resolved);
            if (resolve_rc >= 0)
                resolve_rc = fs::resolve_path(resolved, resolved, sizeof resolved, true);
            if (resolve_rc < 0)
                return cq_post_guarded(ring, sqe.user_data, (i32) resolve_rc);
            u64 sec  = 0;
            u64 nsec = 0;
            if (!memory::realtime_now(sec, nsec))
                return cq_post_guarded(ring, sqe.user_data, -EIO);
            return cq_post_guarded(ring, sqe.user_data,
                                   static_cast<i32>(fs::touch_path(resolved, sec, nsec)));
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor utimensat_descriptor = utimensat::descriptor;

} // namespace astia::io::aio::operations
