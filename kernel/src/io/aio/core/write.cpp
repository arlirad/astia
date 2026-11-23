#include "common.hpp"

namespace astia::io::aio::operations {

    struct write : handler<write, opcode::write> {
        static core::task<aio_result> invoke([[maybe_unused]] ring::ring_slot& slot,
                                             [[maybe_unused]] astia_rings*     ring,
                                             const astia_sqe& sqe) noexcept {
            co_return submit_io(slot, ring, sqe, ::astia::io::direction::output, false)
                ? aio_result::pending
                : aio_result::retry;
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor write_descriptor = write::descriptor;

} // namespace astia::io::aio::operations
