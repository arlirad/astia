#include "../../io/aio/handler.hpp"
#include "common.hpp"

namespace astia::io::aio::operations {

    struct shutdown : handler<shutdown, opcode::shutdown> {
        static bool invoke([[maybe_unused]] ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
            auto object = file_for(sqe.fd);
            if (!object)
                return cq_post_guarded(ring, sqe.user_data, -EBADF);
            const auto* socket = object->node()->unix_socket_capability();
            return cq_post_guarded(
                ring, sqe.user_data,
                static_cast<i32>(socket == nullptr
                                     ? -EOPNOTSUPP
                                     : socket->shutdown(*object, sqe.op_flags)));
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor shutdown_descriptor = shutdown::descriptor;

} // namespace astia::io::aio::operations
