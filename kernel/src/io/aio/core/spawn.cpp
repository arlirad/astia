#include "common.hpp"

namespace astia::io::aio::operations {

    struct spawn : handler<spawn, opcode::spawn> {
        static core::task<aio_result>
        invoke([[maybe_unused]] ring::ring_slot& slot, [[maybe_unused]] astia_rings* ring,
               [[maybe_unused]] const astia_sqe& sqe) noexcept {
            co_return -ENOSYS;
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor spawn_descriptor = spawn::descriptor;

} // namespace astia::io::aio::operations
