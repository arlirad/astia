#include "../../io/aio/handler.hpp"
#ifdef ASTIA_AIO_HOST_TEST
#include "dispatch.hpp"
#else
#include "common.hpp"
#endif

namespace astia::io::aio::operations {

#ifndef ASTIA_AIO_HOST_TEST
    namespace {

        bool handle_unlinkat(ring_slot&, astia_rings* ring, const astia_sqe& s) {
            char path[256];
            if (!memory::copy_in_string(span<char>(path, sizeof path),
                                        memory::user_ptr<char>(s.address)))
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            auto* current = proc::current_process();
            char  resolved[256];
            if (current == nullptr)
                return cq_post_guarded(ring, s.user_data, -ESRCH);
            long resolve_rc = resolve_at(current, s.fd, path, resolved, sizeof resolved);
            if (resolve_rc >= 0)
                resolve_rc = fs::resolve_path(resolved, resolved, sizeof resolved, false);
            if (resolve_rc < 0)
                return cq_post_guarded(ring, s.user_data, (i32) resolve_rc);
            const long rc =
                fs::remove_path(resolved, (s.op_flags & UNLINKAT_REMOVEDIR) != 0);
            return cq_post_guarded(ring, s.user_data, static_cast<i32>(rc));
        }

    } // namespace
#endif

    struct unlinkat : handler<unlinkat, opcode::unlinkat> {
        static bool invoke([[maybe_unused]] ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
#ifdef ASTIA_AIO_HOST_TEST
            return ::astia::io::aio::filesystem::dispatch(slot, ring, sqe);
#else
            return handle_unlinkat(slot, ring, sqe);
#endif
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor unlinkat_descriptor = unlinkat::descriptor;

} // namespace astia::io::aio::operations
