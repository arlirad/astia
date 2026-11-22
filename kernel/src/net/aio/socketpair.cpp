#include "../../io/aio/handler.hpp"
#include "dispatch.hpp"

namespace astia::io::aio::operations {

    struct socketpair : handler<socketpair, opcode::socketpair> {
        static bool invoke(ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
            return ::astia::io::aio::socket::dispatch(slot, ring, sqe);
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor socketpair_descriptor = socketpair::descriptor;

} // namespace astia::io::aio::operations
