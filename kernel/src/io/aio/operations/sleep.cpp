#include "../handler.hpp"
#include "../readiness.hpp"

namespace astia::io::aio::operations {

    struct sleep : handler<sleep, opcode::sleep> {
        static bool invoke(ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
            return readiness::dispatch_registered(slot, ring, sqe);
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor sleep_descriptor = sleep::descriptor;

} // namespace astia::io::aio::operations
