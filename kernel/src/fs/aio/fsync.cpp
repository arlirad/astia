#include "../../io/aio/handler.hpp"
#include "common.hpp"

namespace astia::io::aio::operations {

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

    struct fsync : handler<fsync, opcode::fsync> {
        static bool invoke([[maybe_unused]] ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
            return handle_fsync(ring, sqe);
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor fsync_descriptor = fsync::descriptor;

} // namespace astia::io::aio::operations
