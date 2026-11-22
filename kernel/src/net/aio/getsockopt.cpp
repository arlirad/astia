#include "../../io/aio/handler.hpp"
#include "dispatch.hpp"

namespace astia::io::aio::operations {

    struct getsockopt : handler<getsockopt, opcode::getsockopt> {
        static bool invoke(ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
            return ::astia::io::aio::socket::dispatch(slot, ring, sqe);
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor getsockopt_descriptor = getsockopt::descriptor;

} // namespace astia::io::aio::operations
