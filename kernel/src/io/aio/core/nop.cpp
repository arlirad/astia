#include "common.hpp"

namespace astia::io::aio::operations {

    struct nop : handler<nop, opcode::nop> {
        static bool invoke([[maybe_unused]] ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
            return cq_post_guarded(ring, sqe.user_data, 0);
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor nop_descriptor = nop::descriptor;

} // namespace astia::io::aio::operations
