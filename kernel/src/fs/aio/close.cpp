#include "../../io/aio/handler.hpp"
#include "common.hpp"

namespace astia::io::aio::operations {

    struct close : handler<close, opcode::close> {
        static bool invoke([[maybe_unused]] ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
            proc::process* current = proc::current_process();
            auto           mapped  = mapper<fd_t>::from(sqe.fd);
            if (current == nullptr || !mapped.has_value ||
                !current->descriptor(mapped.value)) {
                return cq_post_guarded(ring, sqe.user_data, -EBADF);
            }
            current->clear_descriptor(mapped.value);
            return cq_post_guarded(ring, sqe.user_data, 0);
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor close_descriptor = close::descriptor;

} // namespace astia::io::aio::operations
