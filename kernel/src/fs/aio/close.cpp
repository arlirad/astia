#include "../../io/aio/handler.hpp"
#include "common.hpp"

namespace astia::io::aio::operations {

    namespace {

        bool handle_close(astia_rings* ring, const astia_sqe& s) {
            proc::process* current = proc::current_process();
            auto           mapped  = mapper<fd_t>::from(s.fd);
            if (current == nullptr || !mapped.has_value ||
                !current->descriptor(mapped.value)) {
                return cq_post_guarded(ring, s.user_data, -EBADF);
            }
            current->clear_descriptor(mapped.value);
            return cq_post_guarded(ring, s.user_data, 0);
        }

    } // namespace

    struct close : handler<close, opcode::close> {
        static bool invoke([[maybe_unused]] ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
            return handle_close(ring, sqe);
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor close_descriptor = close::descriptor;

} // namespace astia::io::aio::operations
