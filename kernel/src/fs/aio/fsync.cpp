#include "../../io/aio/handler.hpp"
#include "common.hpp"

namespace astia::io::aio::operations {

    struct fsync : handler<fsync, opcode::fsync> {
        static bool invoke([[maybe_unused]] ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
            auto object = file_for(sqe.fd);
            if (!object)
                return cq_post_guarded(ring, sqe.user_data, -EBADF);
            if (!object->mode().is_regular())
                return cq_post_guarded(ring, sqe.user_data, -EINVAL);
            return cq_post_guarded(ring, sqe.user_data, static_cast<i32>(object->sync()));
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor fsync_descriptor = fsync::descriptor;

} // namespace astia::io::aio::operations
