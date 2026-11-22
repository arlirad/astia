#include "../dispatch.hpp"
#include "../handler.hpp"

namespace astia::io::aio::operations {

    struct ioctl : handler<ioctl, opcode::ioctl> {
        static bool invoke(ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
            return ::astia::io::aio::dispatch::dispatch_core(slot, ring, sqe);
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor ioctl_descriptor = ioctl::descriptor;

} // namespace astia::io::aio::operations
