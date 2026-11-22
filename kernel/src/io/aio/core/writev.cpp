#include "../handler.hpp"
#include "../dispatch.hpp"

namespace astia::io::aio::operations {

    struct writev : handler<writev, opcode::writev> {
        static bool invoke(ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
            return ::astia::io::aio::dispatch::dispatch_core(slot, ring, sqe);
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor writev_descriptor = writev::descriptor;

} // namespace astia::io::aio::operations
