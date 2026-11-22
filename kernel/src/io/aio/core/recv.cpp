#include "common.hpp"

namespace astia::io::aio::operations {

    struct recv : handler<recv, opcode::recv> {
        static bool invoke([[maybe_unused]] ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
            return submit_io(slot, ring, sqe, ::astia::io::direction::input, false);
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor recv_descriptor = recv::descriptor;

} // namespace astia::io::aio::operations
