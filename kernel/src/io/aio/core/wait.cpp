#include "common.hpp"

namespace astia::io::aio::operations {

    struct wait : handler<wait, opcode::wait> {
        static bool invoke([[maybe_unused]] ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
#ifdef ASTIA_AIO_HOST_TEST
            return ::astia::io::aio::dispatch::dispatch_core(slot, ring, sqe);
#else
            return cq_post_guarded(ring, sqe.user_data, -ENOSYS);
#endif
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor wait_descriptor = wait::descriptor;

} // namespace astia::io::aio::operations
