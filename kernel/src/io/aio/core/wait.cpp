#include "common.hpp"

namespace astia::io::aio::operations {

    struct wait : handler<wait, opcode::wait> {
        static bool invoke([[maybe_unused]] ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
            return cq_post_guarded(ring, sqe.user_data, -ENOSYS);
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor wait_descriptor = wait::descriptor;

} // namespace astia::io::aio::operations
