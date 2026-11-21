#include "../handler.hpp"
#include "../socket.hpp"

namespace astia::io::aio::operations {

    struct recvfrom : handler<recvfrom, opcode::recvfrom> {
        static bool invoke(ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
            return socket::dispatch(slot, ring, sqe);
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor recvfrom_descriptor = recvfrom::descriptor;

} // namespace astia::io::aio::operations
