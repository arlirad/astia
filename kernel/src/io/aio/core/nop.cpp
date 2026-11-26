#include "common.hpp"

namespace astia::io::aio::operations {
    struct nop : handler<nop, opcode::nop> {
        static core::task<aio_result>
        invoke([[maybe_unused]] ring::ring_slot& slot, [[maybe_unused]] astia_rings* ring,
               [[maybe_unused]] astia_sqe     sqe,
               [[maybe_unused]] core::cltoken token) noexcept {
            co_return 0;
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor nop_descriptor = nop::descriptor;
} // namespace astia::io::aio::operations
