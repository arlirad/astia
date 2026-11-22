#include "common.hpp"

namespace astia::io::aio::operations {

    struct readv : handler<readv, opcode::readv> {
        static bool invoke([[maybe_unused]] ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
            return submit_io(slot, ring, sqe, ::astia::io::direction::input, true);
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor readv_descriptor = readv::descriptor;

} // namespace astia::io::aio::operations
