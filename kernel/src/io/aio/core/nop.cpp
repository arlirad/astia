#include "../dispatch.hpp"
#include "../handler.hpp"

namespace astia::io::aio::operations {

    struct nop : handler<nop, opcode::nop> {
        static bool invoke(ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
            return ::astia::io::aio::dispatch::dispatch_core(slot, ring, sqe);
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor nop_descriptor = nop::descriptor;

} // namespace astia::io::aio::operations
