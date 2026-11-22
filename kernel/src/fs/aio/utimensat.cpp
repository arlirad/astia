#include "../../io/aio/handler.hpp"
#ifdef ASTIA_AIO_HOST_TEST
#include "dispatch.hpp"
#else
#include "common.hpp"
#endif

namespace astia::io::aio::operations {

#ifndef ASTIA_AIO_HOST_TEST
    namespace {

        bool handle_utimensat(astia_rings* ring, const astia_sqe& s) {
            char path[256];
            if (!memory::copy_in_string(span<char>(path, sizeof path),
                                        memory::user_ptr<char>(s.address)))
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            auto* current = proc::current_process();
            char  resolved[256];
            if (current == nullptr)
                return cq_post_guarded(ring, s.user_data, -ESRCH);
            long resolve_rc = current->compose_path(path, resolved, sizeof resolved);
            if (resolve_rc >= 0)
                resolve_rc = fs::resolve_path(resolved, resolved, sizeof resolved, true);
            if (resolve_rc < 0)
                return cq_post_guarded(ring, s.user_data, (i32) resolve_rc);
            u64 sec  = 0;
            u64 nsec = 0;
            if (!memory::realtime_now(sec, nsec))
                return cq_post_guarded(ring, s.user_data, -EIO);
            return cq_post_guarded(ring, s.user_data,
                                   static_cast<i32>(fs::touch_path(resolved, sec, nsec)));
        }

    } // namespace
#endif

    struct utimensat : handler<utimensat, opcode::utimensat> {
        static bool invoke([[maybe_unused]] ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
#ifdef ASTIA_AIO_HOST_TEST
            return ::astia::io::aio::filesystem::dispatch(slot, ring, sqe);
#else
            return handle_utimensat(ring, sqe);
#endif
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor utimensat_descriptor = utimensat::descriptor;

} // namespace astia::io::aio::operations
