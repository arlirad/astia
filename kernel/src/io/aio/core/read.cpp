#include "common.hpp"

namespace astia::io::aio::operations {

    struct read : handler<read, opcode::read> {
        static core::task<aio_result>
        invoke([[maybe_unused]] ring::ring_slot& slot, [[maybe_unused]] astia_rings* ring,
               astia_sqe sqe, [[maybe_unused]] core::cltoken token) noexcept {
            co_return submit_io(slot, ring, sqe, ::astia::io::direction::input, false)
                ? aio_result::pending
                : aio_result::retry;
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor read_descriptor = read::descriptor;

} // namespace astia::io::aio::operations
