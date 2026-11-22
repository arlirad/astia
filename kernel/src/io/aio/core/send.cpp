#include "common.hpp"

namespace astia::io::aio::operations {

    struct send : handler<send, opcode::send> {
        static bool invoke([[maybe_unused]] ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
#ifdef ASTIA_AIO_HOST_TEST
            return ::astia::io::aio::dispatch::dispatch_core(slot, ring, sqe);
#else
            return submit_io(slot, ring, sqe, ::astia::io::direction::output, false);
#endif
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor send_descriptor = send::descriptor;

} // namespace astia::io::aio::operations
