#include "../../io/aio/handler.hpp"
#include "dispatch.hpp"

namespace astia::io::aio::operations {

    struct bind : handler<bind, opcode::bind> {
        static bool invoke(ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
            return ::astia::io::aio::socket::dispatch(slot, ring, sqe);
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor bind_descriptor = bind::descriptor;

} // namespace astia::io::aio::operations
