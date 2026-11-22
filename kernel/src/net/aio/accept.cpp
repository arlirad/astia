#include "../../io/aio/handler.hpp"
#include "common.hpp"

namespace astia::io::aio::operations {

    namespace {

        bool handle_accept(ring_slot& slot, astia_rings* ring, const astia_sqe& s) {
            auto object = file_for(s.fd);
            if (!object)
                return cq_post_guarded(ring, s.user_data, -EBADF);
            if (object->node()->unix_socket_capability() == nullptr)
                return cq_post_guarded(ring, s.user_data, -EOPNOTSUPP);
            return dispatch::submit_socket_request(slot, ring, s, object, nullptr, 0,
                                                   ::astia::io::direction::input, true,
                                                   true);
        }

    } // namespace

    struct accept : handler<accept, opcode::accept> {
        static bool invoke([[maybe_unused]] ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
            return handle_accept(slot, ring, sqe);
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor accept_descriptor = accept::descriptor;

} // namespace astia::io::aio::operations
