#include "aio/handler.hpp"
#include "readiness.hpp"

namespace astia::io::aio::operations {

    struct poll_add : handler<poll_add, opcode::poll_add> {
        static bool invoke(ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
            return ::astia::io::aio::readiness::dispatch_registered(slot, ring, sqe);
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor poll_add_descriptor = poll_add::descriptor;

    struct sleep : handler<sleep, opcode::sleep> {
        static bool invoke(ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
            return ::astia::io::aio::readiness::dispatch_registered(slot, ring, sqe);
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor sleep_descriptor = sleep::descriptor;

} // namespace astia::io::aio::operations
