#include "../handler.hpp"
#include "../dispatch.hpp"

namespace astia::io::aio::operations {

    struct fcntl : handler<fcntl, opcode::fcntl> {
        static bool invoke(ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
            return ::astia::io::aio::dispatch::dispatch_core(slot, ring, sqe);
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor fcntl_descriptor = fcntl::descriptor;

} // namespace astia::io::aio::operations
