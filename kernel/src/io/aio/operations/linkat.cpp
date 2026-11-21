#include "../filesystem.hpp"
#include "../handler.hpp"

namespace astia::io::aio::operations {

    struct linkat : handler<linkat, opcode::linkat> {
        static bool invoke(ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
            return filesystem::dispatch(slot, ring, sqe);
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor linkat_descriptor = linkat::descriptor;

} // namespace astia::io::aio::operations
