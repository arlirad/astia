#include "../../io/aio/handler.hpp"
#ifdef ASTIA_AIO_HOST_TEST
#include "dispatch.hpp"
#else
#include "common.hpp"
#endif

namespace astia::io::aio::operations {

#ifndef ASTIA_AIO_HOST_TEST
    namespace {

        bool handle_fsync(astia_rings* ring, const astia_sqe& s) {
            auto object = file_for(s.fd);
            if (!object)
                return cq_post_guarded(ring, s.user_data, -EBADF);
            if (!object->mode().is_regular())
                return cq_post_guarded(ring, s.user_data, -EINVAL);
            return cq_post_guarded(ring, s.user_data, static_cast<i32>(object->sync()));
        }

    } // namespace
#endif

    struct fsync : handler<fsync, opcode::fsync> {
        static bool invoke([[maybe_unused]] ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
#ifdef ASTIA_AIO_HOST_TEST
            return ::astia::io::aio::filesystem::dispatch(slot, ring, sqe);
#else
            return handle_fsync(ring, sqe);
#endif
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor fsync_descriptor = fsync::descriptor;

} // namespace astia::io::aio::operations
