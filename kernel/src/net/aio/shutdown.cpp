#include "../../io/aio/handler.hpp"
#include "common.hpp"

namespace astia::io::aio::operations {

    namespace {

        bool handle_shutdown(astia_rings* ring, const astia_sqe& s) {
            auto object = file_for(s.fd);
            if (!object)
                return cq_post_guarded(ring, s.user_data, -EBADF);
            const auto* socket = object->node()->unix_socket_capability();
            return cq_post_guarded(
                ring, s.user_data,
                static_cast<i32>(socket == nullptr
                                     ? -EOPNOTSUPP
                                     : socket->shutdown(*object, s.op_flags)));
        }

    } // namespace

    struct shutdown : handler<shutdown, opcode::shutdown> {
        static bool invoke([[maybe_unused]] ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
            return handle_shutdown(ring, sqe);
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor shutdown_descriptor = shutdown::descriptor;

} // namespace astia::io::aio::operations
