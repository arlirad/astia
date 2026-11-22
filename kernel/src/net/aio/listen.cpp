#include "../../io/aio/handler.hpp"
#include "common.hpp"

namespace astia::io::aio::operations {

    namespace {

        bool handle_listen(astia_rings* ring, const astia_sqe& s) {
            auto object = file_for(s.fd);
            if (!object)
                return cq_post_guarded(ring, s.user_data, -EBADF);
            const auto* socket = object->node()->unix_socket_capability();
            return cq_post_guarded(
                ring, s.user_data,
                static_cast<i32>(socket == nullptr ? -EOPNOTSUPP
                                                   : socket->listen(*object, s.aux)));
        }

    } // namespace

    struct listen : handler<listen, opcode::listen> {
        static bool invoke([[maybe_unused]] ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
            return handle_listen(ring, sqe);
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor listen_descriptor = listen::descriptor;

} // namespace astia::io::aio::operations
