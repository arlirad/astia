#include "common.hpp"

namespace astia::io::aio::operations {

    struct write : handler<write, opcode::write> {
        static bool invoke([[maybe_unused]] ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
            return submit_io(slot, ring, sqe, ::astia::io::direction::output, false);
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor write_descriptor = write::descriptor;

} // namespace astia::io::aio::operations
