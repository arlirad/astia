#include "../../io/aio/handler.hpp"
#include "common.hpp"

namespace astia::io::aio::operations {

    struct listen : handler<listen, opcode::listen> {
        static bool invoke([[maybe_unused]] ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
            auto object = file_for(sqe.fd);
            if (!object)
                return cq_post_guarded(ring, sqe.user_data, -EBADF);
            const auto* socket = object->node()->unix_socket_capability();
            return cq_post_guarded(
                ring, sqe.user_data,
                static_cast<i32>(socket == nullptr ? -EOPNOTSUPP
                                                   : socket->listen(*object, sqe.aux)));
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor listen_descriptor = listen::descriptor;

} // namespace astia::io::aio::operations
