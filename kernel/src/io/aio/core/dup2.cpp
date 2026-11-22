#include "../handler.hpp"
#include "../dispatch.hpp"

namespace astia::io::aio::operations {

    struct dup2 : handler<dup2, opcode::dup2> {
        static bool invoke(ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
            return ::astia::io::aio::dispatch::dispatch_core(slot, ring, sqe);
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor dup2_descriptor = dup2::descriptor;

} // namespace astia::io::aio::operations
