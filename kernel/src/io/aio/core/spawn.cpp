#include "common.hpp"

namespace astia::io::aio::operations {

    struct spawn : handler<spawn, opcode::spawn> {
        static bool invoke([[maybe_unused]] ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
            return cq_post_guarded(ring, sqe.user_data, -ENOSYS);
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor spawn_descriptor = spawn::descriptor;

} // namespace astia::io::aio::operations
