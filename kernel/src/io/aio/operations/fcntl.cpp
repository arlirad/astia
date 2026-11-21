#include "../dispatch.hpp"
#include "../handler.hpp"

namespace astia::io::aio::operations {

    struct fcntl : handler<fcntl, opcode::fcntl> {
        static bool invoke(ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
            return dispatch::dispatch_core(slot, ring, sqe);
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor fcntl_descriptor = fcntl::descriptor;

} // namespace astia::io::aio::operations
