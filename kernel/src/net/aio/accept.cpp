#include "../../io/aio/handler.hpp"
#include "common.hpp"

namespace astia::io::aio::operations {

    struct accept : handler<accept, opcode::accept> {
        static bool invoke([[maybe_unused]] ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
            auto object = file_for(sqe.fd);
            if (!object)
                return cq_post_guarded(ring, sqe.user_data, -EBADF);
            if (object->node()->unix_socket_capability() == nullptr)
                return cq_post_guarded(ring, sqe.user_data, -EOPNOTSUPP);
            return dispatch::submit_socket_request(slot, ring, sqe, object, nullptr, 0,
                                                   ::astia::io::direction::input, true,
                                                   true);
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor accept_descriptor = accept::descriptor;

} // namespace astia::io::aio::operations
