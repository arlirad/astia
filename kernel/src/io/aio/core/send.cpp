#include "common.hpp"

namespace astia::io::aio::operations {

    struct send : handler<send, opcode::send> {
        static core::task<aio_result> invoke([[maybe_unused]] ring::ring_slot& slot,
                                             [[maybe_unused]] astia_rings*     ring,
                                             astia_sqe                         sqe,
                                             core::cltoken token) noexcept {
            co_return submit_io(slot, ring, sqe, ::astia::io::direction::output, false,
                                token)
                ? aio_result::pending
                : aio_result::retry;
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor send_descriptor = send::descriptor;

} // namespace astia::io::aio::operations
