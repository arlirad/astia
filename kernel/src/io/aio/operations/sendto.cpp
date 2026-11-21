#include "../handler.hpp"
#include "../socket.hpp"

namespace astia::io::aio::operations {

    struct sendto : handler<sendto, opcode::sendto> {
        static bool invoke(ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
            return socket::dispatch(slot, ring, sqe);
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor sendto_descriptor = sendto::descriptor;

} // namespace astia::io::aio::operations
