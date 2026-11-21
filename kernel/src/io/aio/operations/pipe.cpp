#include "../handler.hpp"
#include "../socket.hpp"

namespace astia::io::aio::operations {

    struct pipe : handler<pipe, opcode::pipe> {
        static bool invoke(ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
            return socket::dispatch(slot, ring, sqe);
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor pipe_descriptor = pipe::descriptor;

} // namespace astia::io::aio::operations
