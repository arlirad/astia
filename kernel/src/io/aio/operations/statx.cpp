#include "../filesystem.hpp"
#include "../handler.hpp"

namespace astia::io::aio::operations {

    struct statx : handler<statx, opcode::statx> {
        static bool invoke(ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
            return filesystem::dispatch(slot, ring, sqe);
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor statx_descriptor = statx::descriptor;

} // namespace astia::io::aio::operations
